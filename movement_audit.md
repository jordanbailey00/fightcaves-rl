# Movement Audit

## Scope

This audit compares the current Fight Caves backend and viewer movement behavior against the reference implementations in:

- `/home/joe/projects/runescape-rl-reference/rsmod`
- `/home/joe/projects/runescape-rl-reference/runelite`
- `/home/joe/projects/runescape-rl-reference/client3`
- `/home/joe/projects/runescape-rl-reference/void_rsps`
- `/home/joe/projects/runescape-rl-reference/2011Scape-game`

Current repo files reviewed:

- `runescape-rl/fc-core/include/fc_contracts.h`
- `runescape-rl/fc-core/src/fc_state.c`
- `runescape-rl/fc-core/src/fc_tick.c`
- `runescape-rl/fc-core/src/fc_pathfinding.c`
- `runescape-rl/fc-core/src/fc_npc.c`
- `runescape-rl/fc-core/src/fc_combat.c`
- `runescape-rl/training-env/fight_caves.h`
- `runescape-rl/demo-env/src/viewer.c`

Reference files reviewed include RSMod route and game-process code, RuneLite `WorldArea` and collision flags, Client3 pathing/collision map code, Void RSPS movement/combat movement, and 2011Scape movement queue, path requests, and combat path actions.

## Executive Summary

The current implementation is playable, but movement is split across three different surfaces that do not share one authoritative movement model:

1. Policy actions use low-level movement heads and action masks.
2. Backend combat auto-approach builds routes internally.
3. The playable viewer directly mutates player routes and targets.

That makes manual play, policy behavior, debug pathing, and rendered animation diverge from each other. The largest correctness gaps are:

- Diagonal movement collision is inconsistent between action masks, route movement, player movement, and NPC movement.
- Run actions appear to use asymmetric deltas for diagonals.
- Combat auto-approach routes to NPC center tiles instead of valid perimeter attack tiles.
- NPC movement speed is currently one tile per tick, which matches the reference repos for normal NPC combat movement, but player run behavior needs cleanup.
- The active TBow backend range is 10, but one debug range overlay is hardcoded to 7, so the viewer can visually under-report max range.
- Combat style UI selection does not currently drive backend weapon speed/range, so Accurate/Rapid/Longrange buttons can mislead manual testing.
- Player attack timing is likely one tick too fast because the timer is decremented in the same tick it is set.
- NPCs can stop moving when in nominal range but blocked by LOS.
- Viewer projectiles and animations are inferred from timers instead of explicit core events, which makes ranged/magic attacks visually unreliable.

The highest-value fix is to make the backend expose one shared step validator, one attack reachability check, and explicit per-tick render events. The viewer should consume those events instead of guessing from state deltas.

## Current System Summary

### Backend Action Surface

`fc_contracts.h` defines seven action heads:

- head 0: movement
- head 1: attack target
- head 2: eat
- head 3: drink
- head 4: prayer
- head 5: walk target X
- head 6: walk target Y

Training currently exposes only five heads in `training-env/fight_caves.h` through `FC_PUFFER_NUM_ATNS 5`. That means heads 5 and 6 are not available to the policy, even though they are part of the core action contract.

### Player Movement

Player movement is handled in `fc_tick.c` by either consuming an existing route or applying a low-level movement command. Route movement uses `fc_move_toward(..., max_steps=2)`. Direct movement also uses `fc_move_toward` for run commands.

Action validity is computed in `fc_state.c`. `move_action_valid` checks the destination tile for walk/run actions, but does not validate the intermediate run tile or diagonal side blockers.

### Combat Movement

Player combat actions set `attack_target_idx` and `approach_target`. If the target is not currently attackable, `fc_tick.c` attempts to build a BFS path to the target NPC center and trims that route to the first tile that is in weapon range and LOS.

Player attacks are currently ranged-only in backend combat. The UI can display combat and magic tabs, but the backend does not currently have separate authoritative player melee or magic attack modes.

### Movement Speed And Attack Range

Current player movement:

- Route movement consumes one step when walking and two route steps when `p->is_running` is enabled.
- Low-level movement has separate walk and run actions. Walk moves one tile. Run attempts up to two tiles.
- `p->is_running` is initialized on and `run_energy` is initialized to 10000, but run energy does not appear to drain or regenerate in the core tick loop.
- Run diagonal action deltas are asymmetric, for example NE is a one-X/two-Y target, not two repeated NE steps.

Current NPC movement:

- All Fight Caves NPC stats currently use `movement_speed = 1`.
- NPCs therefore move one tile per tick when chasing.
- That broadly matches the reference implementations for normal NPC combat walking. RSMod, 2011Scape, and Void process movement as one walk step plus an optional second run step; NPC combat movement generally consumes one step unless explicitly running or forced.

Current player attack range:

- `FC_ACTIVE_LOADOUT` is loadout 1, Masori plus Twisted Bow.
- That loadout sets `weapon_speed = 5` and `weapon_range = 10`.
- Backend attack range uses `fc_distance_to_npc`, which measures Chebyshev distance to the nearest tile of the NPC footprint. This is the right shape for large NPCs.
- The 2D debug range overlay in `fc_debug_overlay.h` is hardcoded to `range = 7`, so it is wrong for the active TBow loadout.
- The viewer combat-style UI displays Accurate/Rapid/Longrange, but backend attacks continue using the fixed `p->weapon_speed` and `p->weapon_range`. Manual UI selection does not currently change the authoritative attack range or speed.

### NPC Movement

NPCs use greedy movement through `fc_npc_step_toward_sized`. NPCs only move when tile distance is greater than their attack range. If they are in nominal range but LOS is blocked, the attack does not fire and the NPC does not move to regain LOS.

### Viewer Rendering

The viewer renders movement and attacks mostly by inference:

- Player movement is interpolated from previous tile to current tile.
- Player attack animations are selected from `attack_timer`.
- Player projectiles are spawned by checking `attack_timer == 5`.
- NPC attack animation uses `npc_attack_visual_timer` or timer checks.
- NPC facing is drawn toward the player regardless of the NPC's movement or current target.

The playable viewer also directly mutates `p->route_x`, `p->route_y`, `p->route_len`, `p->route_idx`, `p->attack_target_idx`, and `p->approach_target`. It does not send the same movement or attack target actions that the policy uses.

## Reference Patterns

### RSMod

RSMod has a centralized route and step validation model:

- `StepValidator.canTravel(...)` validates directional clipping, sizes, and diagonal side blockers.
- `LineValidator.hasLineOfSight(...)` and `hasLineOfWalk(...)` use separate collision flags for line checks.
- `RouteFactory.create(...)` can route to a `PathingEntityAvatar` using the target entity dimensions.
- `PlayerInteractionProcessor` performs pre-movement interaction checks, route processing, movement, then post-movement interaction checks.
- `NpcInteractionProcessor`, `PlayerMovementProcessor`, and `NpcMovementProcessor` keep route destination state, processed coordinates, and collision occupancy updates.

The important pattern is that interaction range, LOS, movement, and entity size are evaluated together. The route target is a rectangle/entity footprint, not a single center tile.

### RuneLite

RuneLite's `WorldArea` exposes behavior that matches the game-client model:

- `canTravelInDirection(...)` checks directional collision flags for the whole area.
- Diagonal movement checks both cardinal side directions.
- `hasLineOfSightTo(...)` checks line of sight between area edges, not just center points.
- `CollisionDataFlag` separates movement blocking from LOS blocking.

This is useful as a compact correctness reference for tile-size movement and LOS behavior.

### Client3

Client3 separates path queue state from immediate coordinates:

- `PathingEntity` tracks queued path tiles and running flags.
- Movement pushes steps into a queue and consumes them over client cycles.
- `collisionmap.c` has classic directional clipping flags plus range/projectile blocking.

This is relevant for viewer rendering. The client knows walk/run step direction and path queue state instead of inferring movement only from final tile deltas.

### Void RSPS

Void RSPS has a clear combat movement loop:

- `CombatMovement.tick` faces/watches the target, checks whether the target is attackable, and recalculates movement if not.
- `Movement.arrived(...)` accounts for range and line of sight.
- Movement exposes walk and run steps separately for visuals.
- Characters can step out when overlapping.

The key behavior is that being within numeric distance is not enough. If LOS or attack reachability fails, movement continues.

### 2011Scape

2011Scape has useful combat-pathing semantics:

- `MovementQueue.cycle` consumes one walk step and optional run step per cycle.
- `PawnPathAction.walkTo` builds a `PathRequest` with source size, target size, touch radius, projectile path, and clipping flags.
- Combat walks to the target range, stops movement, and then attacks.
- Pathing reroutes if the target moves while walking.

This gives us a good model for "move into attack range, stop/settle, then attack" behavior.

## Findings

### 1. Diagonal Collision Is Inconsistent

Severity: High

`fc_move_toward` checks only the diagonal destination tile. The route code's greedy path checks both cardinal side tiles before accepting a diagonal step. `fc_npc_step_toward_sized` checks only whether the destination footprint is walkable and does not validate swept side blockers for diagonal movement.

`move_action_valid` validates the final walk/run destination, but not the intermediate run tile or diagonal side blockers.

Why this matters:

- The action mask can mark a movement legal even though route movement would reject it.
- Direct player movement can corner-cut.
- NPCs and players do not obey the same clipping rules.
- Debug reachable tiles can disagree with actual low-level movement.

Reference behavior:

- RSMod `StepValidator` and RuneLite `WorldArea.canTravelInDirection` both validate diagonal side blockers.
- Client3 collision map uses directional wall flags rather than destination-only walkability.

Fix direction:

- Add one shared `fc_can_step_size(state, x, y, size, dx, dy)` validator.
- Use it in `move_action_valid`, `fc_move_toward`, BFS neighbor expansion, greedy route fallback, and `fc_npc_step_toward_sized`.
- For diagonal movement, require destination plus both cardinal side paths to be passable for the whole footprint.

### 2. Run Action Deltas Look Asymmetric

Severity: High

`fc_contracts.h` defines walk diagonals as expected, but run diagonals are:

- NE: `(+1, -2)`
- SE: `(+1, +2)`
- SW: `(-1, +2)`
- NW: `(-1, -2)`

Cardinal run is two tiles, such as east `(+2, 0)`. With `fc_move_toward(..., max_steps=2)`, run NE becomes one diagonal step and one vertical step, not two northeast steps.

Why this matters:

- Policy and manual control get a skewed movement primitive.
- Action masks report movement availability for a final tile that does not match intuitive run movement.
- Render interpolation can look wrong because the logical run path is not a straight two-tile diagonal.

Reference behavior:

- 2011Scape, RSMod, Void RSPS, and Client3 all model run as an optional second step from the path queue, not as an asymmetric final delta.

Fix direction:

- Decide whether diagonal run should be `(+2, -2)` for two diagonal steps or whether run should be removed from low-level action heads.
- Validate both steps independently.
- Expose the intermediate step to rendering.

### 3. Training Does Not Expose Route Target Heads

Severity: Medium

The core action contract includes walk target X/Y heads, but `training-env/fight_caves.h` exposes only five action heads to PufferLib. Heads 5 and 6 are dead for training.

Why this matters:

- Backend supports a route target interface the policy cannot use.
- Viewer click-to-walk uses direct route mutation, so manual play tests a different surface than the policy.

Reference behavior:

- References route from selected destinations and then consume movement steps through the same movement processor.

Fix direction:

- Either expose route target heads to training or remove them from the core contract.
- If retained, make the viewer send route target actions instead of mutating route arrays directly.
- Keep one movement pipeline for policy, manual play, and eval.

### 4. Combat Auto-Approach Routes To NPC Center

Severity: High

In `fc_tick.c`, combat approach computes a route to:

- `target->x + target->size / 2`
- `target->y + target->size / 2`

It then trims the returned route to a tile that is in range and LOS.

Why this matters:

- Large NPC center tiles can be inside the NPC footprint or otherwise unreachable.
- A valid perimeter attack tile may exist even when BFS to the center fails.
- This is especially relevant for Jad and larger NPCs.

Reference behavior:

- RSMod routes to pathing entities with target dimensions.
- 2011Scape `PathRequest` includes target width and length.
- Void combat movement evaluates arrival against range and target bounds, not center tile.

Fix direction:

- Build combat routes to the nearest valid attack tile around the target footprint.
- The target condition should be "within weapon range and LOS to target footprint."
- BFS should stop when that condition is satisfied instead of routing to the center first.

### 5. Combat Approach Routes Can Go Stale

Severity: Medium

Approach route generation only happens when the current route has been consumed. If the target moves, LOS changes, the player enters attack range early, or the route becomes invalid, the existing route can continue being consumed.

Why this matters:

- The player can overshoot or keep moving after becoming attack-ready.
- The viewer can show awkward movement after an attack target is already reachable.
- Target movement is not handled like reference servers.

Reference behavior:

- RSMod processors reroute when the destination is small or nearly empty.
- 2011Scape `PawnPathAction` reroutes if the target moved while walking.

Fix direction:

- Before consuming a route, check whether the current target is already attackable. If yes, clear/stop the route.
- If the target moved or the next route step is no longer valid, recompute.
- Store route destination metadata so staleness is explicit.

### 6. Attack/Movement Tick Ordering Adds Input Lag

Severity: Medium

`process_player_actions` consumes movement before target selection and approach route generation. A click attack sets the target after movement has already happened, so the generated approach route cannot be consumed until the next tick.

Why this matters:

- Manual play and eval can feel one tick sluggish.
- The agent may spend extra ticks idle after selecting an attack target.

Reference behavior:

- RSMod does pre-movement interaction, route processing, movement, then post-movement interaction.
- 2011Scape combat path actions can walk to range and attack after arrival.

Fix direction:

- Split player tick into:
  1. read action and update interaction target,
  2. check pre-movement attack,
  3. compute or update movement route,
  4. consume movement,
  5. check post-movement attack.

### 7. Player Attack Timer Is Likely One Tick Too Fast

Severity: High

Player attack code sets `p->attack_timer = p->weapon_speed`. Later in the same tick, `decrement_player_timers` decrements it immediately. That means a speed 5 weapon effectively starts the next tick at 4.

NPC attack timers do not appear to have the same same-tick decrement pattern after attacking.

Why this matters:

- Player DPS can be one tick faster than intended.
- Viewer projectile spawn logic checks for `attack_timer == 5`, which may never be observed after `fc_step`.
- Policy training and visual replay can disagree on when attacks happened.

Reference behavior:

- Reference servers generally process cycle timers consistently and expose attack events separately from countdown state.

Fix direction:

- Decrement player attack timer before attack processing, or skip decrementing newly set timers in the same tick.
- Use a per-tick `player_attack_event` for rendering and analytics instead of reading timer equality.

### 8. NPCs Stop When In Range But LOS Is Blocked

Severity: High

`fc_npc_tick` moves Jad/generic NPCs only when tile distance is greater than attack range. If distance is within range but `fc_has_los_to_npc` fails, attack returns without firing and movement does not continue.

Why this matters:

- NPCs can become passive behind obstacles even though they should reposition.
- Combat movement around pillars can feel wrong.
- Agent strategy may exploit range-only arrival instead of true attack reachability.

Reference behavior:

- Void RSPS `CombatMovement` moves if the target is not attackable, where attackable includes range and LOS.
- RSMod interaction range checks use AP range and LOS.
- 2011Scape path requests can include projectile path checks.

Fix direction:

- Change NPC arrival from `dist <= attack_range` to `fc_npc_can_attack_player(...)`.
- If not attackable, move toward the nearest valid attack tile.

### 9. NPC Movement Is Greedy Only

Severity: Medium

NPCs currently use a simple greedy step toward the player or Jad. They do not pathfind around pillars or route toward a valid attack perimeter.

Why this matters:

- NPCs can stick on geometry.
- Healers moving toward Jad or the player can behave unlike the game.
- Debug pathing and NPC pathing disagree.

Reference behavior:

- RSMod and 2011Scape route NPC movement through the same route/step model.
- Void RSPS movement recalculates paths through target strategies.

Fix direction:

- Add a short NPC BFS route cache, or reuse existing route arrays for NPCs.
- Route to valid attack/heal positions instead of greedy-stepping toward center points.

### 10. Movement, LOS, And Projectile Blocking Use One Binary Walkable Flag

Severity: Medium

The core collision model uses `walkable` as the shared source for movement and LOS. `fc_has_line_of_sight` uses a Bresenham-style walkability check.

Why this matters:

- A tile can block walking but not projectiles, or block projectiles but not movement.
- Pillar LOS behavior may be inaccurate.
- Ranged and magic attack movement cannot match OSRS-style clipping without separate flags.

Reference behavior:

- RSMod and RuneLite separate movement and LOS flags.
- Client3 has movement clipping flags and `blockrange` flags.

Fix direction:

- Extend generated collision assets to include directional movement flags and range/LOS flags.
- Keep binary walkability for simple debug overlays, but do not use it as the only gameplay collision source.

### 11. Core Movement Ignores Entity Occupancy

Severity: Medium

Player and NPC movement do not generally account for occupied tiles. Entities can move through or stand on each other except where special combat checks prevent it.

Why this matters:

- Large NPC and healer movement can overlap incorrectly.
- Player melee-range behavior and step-out behavior cannot be accurate.

Reference behavior:

- RSMod movement processors update block-walk collision for players and NPCs.
- 2011Scape checks NPC occupancy in movement traversal.
- Void RSPS has step-out behavior when overlapping.

Fix direction:

- Add optional occupancy checks to step validation.
- Decide which entities block which movement types in Fight Caves.
- Add step-out handling for overlapping positions.

### 12. Backend Player Combat Is Ranged-Only

Severity: Medium

The backend player attack path always uses ranged combat data. There is no authoritative player melee or magic attack mode.

Why this matters:

- UI attack style and magic tabs cannot currently produce matching backend behavior.
- "Stopping to auto attack/ranged attack/mage attack" can only be accurately fixed for ranged until backend combat styles exist.

Reference behavior:

- 2011Scape combat strategies decide whether a pawn can attack and how it moves based on the selected combat mode.

Fix direction:

- If melee/magic are gameplay goals, add an explicit combat mode/action head and per-style attack range, LOS, animation, projectile, and cooldown behavior.
- If not, hide or mark non-ranged UI controls as visual-only/debug-only.

### 13. Auto-Retaliate Does Not Chase

Severity: Low

`fc_combat.c` can set `attack_target_idx` during auto-retaliate, but it sets `approach_target = 0`. The player will only retaliate if already in range.

Why this matters:

- Auto-retaliate behavior is weaker than expected.
- Manual viewer behavior can feel inconsistent after being attacked.

Reference behavior:

- Combat systems generally keep a target interaction mode and path toward it if not attackable.

Fix direction:

- Decide if auto-retaliate should chase. If yes, set `approach_target = 1` and route through the same combat movement pipeline.

### 14. Route Length Cap Can Silently Truncate Paths

Severity: Low

`FC_MAX_ROUTE` is 64. BFS can return a truncated route without higher-level handling.

Why this matters:

- Long click routes or approach paths can stop short.
- Viewer click movement may look broken on long destinations.

Reference behavior:

- Client path queues are bounded, but servers keep route destination state and continue processing/re-routing across cycles.

Fix direction:

- Store destination metadata and continue/re-route when the route cap is reached.
- Surface route truncation in debug UI.

### 15. Viewer Manual Play Bypasses Policy Actions

Severity: High

`viewer.c` mutates player target and route state directly on click. `build_human_actions` only sends idle movement plus eat/drink/prayer actions.

Why this matters:

- Manual play does not test the same interface used by policy training.
- Movement bugs can be hidden in one surface and visible in another.
- Debug results can be misleading when evaluating agent behavior.

Reference behavior:

- Client input becomes route requests or interaction requests, then the same movement processor consumes them.

Fix direction:

- Make viewer input produce core actions or a small explicit input API that feeds the same backend route/interaction pipeline.
- Stop mutating route arrays directly from viewer code.

### 16. Viewer Projectile Spawn Detection Is Fragile

Severity: High

The viewer spawns player projectiles by checking `attack_target_idx >= 0` and `attack_timer == 5` after stepping the simulation. Because the player attack timer is decremented in the same tick it is set, this condition may never hold.

Why this matters:

- Player ranged projectiles can disappear or spawn late.
- This can make it look like attacks are not happening even when backend damage is correct.

Reference behavior:

- Client/server protocols expose animation, spotanim, and projectile events. Rendering does not infer projectiles from cooldown timers.

Fix direction:

- Add per-tick core events for player attacks, NPC attacks, projectiles, and impact spotanims.
- Spawn viewer projectiles from those events.

### 17. Viewer Animation Selection Is Timer-Based

Severity: Medium

Player and NPC animations are selected from cooldown timers and broad state flags rather than explicit attack/move events.

Why this matters:

- Jad attack animations can drift from actual attack style.
- Player attack animation can trigger during cooldown instead of exactly on attack.
- Food/prayer and other one-tick events can be missed if checked after reset.

Reference behavior:

- Client pathing entities carry movement steps, animation ids, and spotanim updates as explicit state.

Fix direction:

- Drive animation state from core render events.
- Include attack style, animation id, source tile, target tile, and projectile/spotanim id.

### 18. NPC Walk Animation Uses Player Movement Flag

Severity: Medium

NPC animation update appears to check `state.movement_this_tick`, which is a player movement flag, before deciding whether NPCs are walking.

Why this matters:

- NPCs may not play walk animation unless the player also moved.
- NPC movement can look like sliding or idle drifting.

Reference behavior:

- References track movement per pathing entity.

Fix direction:

- Add per-NPC movement flags or compare previous/current NPC coordinates directly without gating on player movement.

### 19. NPC Facing Is Always Toward Player

Severity: Low

In draw code, NPC facing is computed from NPC render position to player position. That applies even for healers moving toward Jad or NPCs that should face their movement direction.

Why this matters:

- Healers and NPCs can visually face the wrong target.
- Attack and movement animations can look wrong even if backend state is correct.

Reference behavior:

- Void RSPS explicitly faces/watches targets.
- Client pathing entities have facing and movement orientation state.

Fix direction:

- Add backend-facing state per NPC.
- Face attack target during attacks, movement direction during movement, and keep previous facing while idle.

### 20. Run Rendering Interpolates One Segment

Severity: Low

The viewer interpolates from previous tile to current tile. For run movement, that hides the intermediate step and draws a straight-line movement segment.

Why this matters:

- Two-step movement can visually cut corners.
- Animation speed and position may not match backend movement.

Reference behavior:

- RSMod, 2011Scape, Void RSPS, and Client3 all preserve walk/run step information separately.

Fix direction:

- Emit walk step and optional run step from backend.
- Interpolate through the intermediate tile during run movement.

### 21. NPC Movement Speed Matches References, But Should Be Locked By Tests

Severity: Low

All Fight Caves NPC stats currently set `movement_speed = 1`. `fc_npc_tick` loops over `movement_speed`, so active NPCs chase one tile per tick.

Why this matters:

- This part looks broadly correct compared to the references. Void movement takes one walk step, with a second step only when the character is running. 2011Scape similarly consumes one walk step and optional run step. RSMod reports one pending step as Walk and two as Run.
- Because NPC movement speed is a gameplay-sensitive constant, accidental changes to `movement_speed = 2` would make mobs feel far too fast.

Reference behavior:

- 2011Scape `MovementQueue.cycle` consumes one walk step and only consumes a second step when running.
- Void `Movement.tick` calls `step(runStep = false)` and only applies `step(runStep = true)` when `character.running`.
- RSMod resolves `pendingStepCount == 1` as Walk and `pendingStepCount == 2` as Run.

Fix direction:

- Keep Fight Caves NPC `movement_speed = 1` unless a specific NPC is proven to run.
- Add a test that all current Fight Caves NPCs move at most one tile per tick while chasing.

### 22. Player Run Speed Is Mechanically Right, But Run State Is Too Loose

Severity: Medium

The player can move two route steps per tick when `p->is_running` is enabled, which matches the reference model for running. However, run energy is initialized to 10000 and only used by the action mask. It does not appear to drain or regenerate in the tick loop.

Why this matters:

- Unlimited running may be intentional for this RL task, but it is not OSRS-like.
- Low-level run actions can be selected even if `p->is_running` is false, because the action type itself encodes run. Route movement uses `p->is_running`. Those are two different run control models.
- The diagonal run deltas are asymmetric, so low-level run direction does not represent two repeated steps in the selected diagonal direction.

Reference behavior:

- References model running as a second path step in the same cycle, not as an arbitrary two-tile destination.
- Client/render state preserves walk and run step directions separately.

Fix direction:

- Decide whether this environment intentionally has unlimited run. If yes, document it and remove misleading run-energy gating.
- If OSRS-like run matters, drain/regenerate run energy and make both route and low-level actions obey one run-mode model.
- Fix diagonal run actions so they are two sequential steps or remove low-level run diagonals in favor of route/path steps.

### 23. Debug Range Overlay Under-Reports Active TBow Range

Severity: High

The active loadout is `FC_ACTIVE_LOADOUT = 1`, Masori plus Twisted Bow, with `weapon_range = 10`. Backend attacks check `dist <= p->weapon_range`, but `fc_debug_overlay.h` draws the 2D attack range ring with a hardcoded `range = 7`.

Why this matters:

- The backend can attack from 10 tiles, but the visible range ring says 7.
- This can make correct max-range attacks look wrong, and can make the player appear not to attack from max range when the overlay is the bad signal.
- The viewer side panel does show `Range: %d tiles` from `p->weapon_range`, so the text and the debug ring can disagree.

Reference behavior:

- 2011Scape ranged strategy clamps ranged attack range to a max of 10, and Longrange can add range up to that cap.

Fix direction:

- Change debug range drawing to use `state->player.weapon_range`.
- If a future combat style changes range, the overlay should read the effective backend range, not a local UI constant.

### 24. Combat Style UI Does Not Drive Backend Speed Or Range

Severity: Medium

The viewer has combat-style buttons and displays style-specific speed text, but backend attack behavior uses fixed `p->weapon_speed` and `p->weapon_range`. The active loadout is fixed to TBow rapid speed 5 and range 10. Selecting Accurate or Longrange in the viewer does not appear to update the core attack speed or range used by `fc_tick.c`.

Why this matters:

- Manual testing can misread style behavior.
- Longrange visually suggests an attack range change, but the active TBow loadout is already at range 10, and backend does not apply the UI style anyway.
- Accurate can display slower speed while backend remains speed 5 if `p->weapon_speed` is unchanged.

Reference behavior:

- 2011Scape computes ranged attack range from weapon type and selected attack style. Longrange adds range, capped at 10.
- Combat movement uses the selected strategy's range to walk into attack range.

Fix direction:

- Either make combat-style UI display read-only for the fixed RL loadout, or wire style selection into backend effective attack speed/range.
- If wired, add a single function such as `fc_player_effective_weapon_range()` and `fc_player_effective_weapon_speed()` so pathing, attack checks, UI, and debug overlays all agree.

## Recommended Implementation Plan

### Phase 1: Backend Movement Correctness

1. Add `fc_can_step_size` as the single source of truth for step validation.
2. Use it in action masks, direct movement, route movement, BFS, greedy fallback, and NPC stepping.
3. Fix run action deltas and validate the intermediate run tile.
4. Decide and document run-energy semantics. Either implement drain/regeneration or remove misleading run-energy gating from the training/viewer surface.
5. Make attack approach path to valid perimeter attack tiles rather than NPC centers.
6. Recompute or clear stale combat routes when the target becomes attackable, moves, or LOS changes.
7. Change NPC movement to chase when not truly attackable, not only when out of numeric range.
8. Fix player attack timer ordering so weapon speed is not shortened by same-tick decrement.
9. Add effective weapon speed/range helpers so pathing, attack checks, UI, and debug overlays use the same values.

### Phase 2: Viewer/Event Parity

1. Add a small per-tick render event buffer to core state.
2. Emit events for player attack, NPC attack, projectile launch, impact spotanim, movement step, eat, drink, prayer change, death, and target change.
3. Update the viewer to spawn projectiles and select animations from those events.
4. Track per-entity movement and facing instead of using global movement flags.
5. Make playable clicks call the same backend route/interaction path used by training/eval.

### Phase 3: Collision Asset Upgrade

1. Extend collision generation to include directional movement clipping.
2. Add separate projectile/LOS blocking flags.
3. Keep current binary walkability only as a derived debug overlay.
4. Add optional dynamic occupancy flags for player/NPC blocking.

### Phase 4: Combat Style Cleanup

1. Decide whether player melee and magic are real gameplay modes.
2. If yes, add explicit combat style selection to action space and backend combat.
3. If no, keep UI controls but do not let them imply backend melee/magic behavior.
4. Add per-style animation/projectile/LOS/range definitions.

### Phase 5: Debug UI Improvements

1. Show current route destination, route length, and whether route was truncated.
2. Show current attack target, approach target, and whether target is attackable this tick.
3. Show movement validator output for hovered tile, including diagonal side blocker reason.
4. Show LOS/projection blocker reason separately from movement walkability.
5. Show per-entity facing and movement state.
6. Draw attack range from the player's effective backend range instead of a hardcoded range.
7. Show effective attack speed/range beside selected UI combat style so stale UI state is obvious.

## Tests To Add

### Movement Validator Tests

- Player cannot diagonal corner-cut when either cardinal side tile is blocked.
- NPC sized movement obeys the same diagonal blocker rules as player movement.
- Action mask validity matches actual movement execution for every low-level movement action.
- Run action validates both steps and lands on the expected final tile.
- Current Fight Caves NPCs move at most one tile per tick while chasing.
- Run energy either changes according to documented OSRS-like rules or is explicitly documented as intentionally unlimited.

### Route Tests

- BFS can route to a valid perimeter attack tile for a large NPC whose center is unreachable.
- A stale route is cleared when the player becomes attackable before consuming the full route.
- Attack route stopping distance uses effective weapon range, not a duplicated UI/debug value.
- Long routes either continue/re-route or report truncation.

### Combat Movement Tests

- NPC in nominal range but blocked by LOS continues moving until attackable.
- Player attack cadence matches `weapon_speed`.
- Auto-retaliate either deliberately does not chase or chases through the same approach path.

### Viewer Tests

- Player projectile spawns on a core attack event, not timer equality.
- NPC walk animation plays when only the NPC moves.
- Jad attack animation and projectile style match the backend attack style event.
- Run interpolation passes through the intermediate step.
- Debug range overlay uses `state->player.weapon_range` or the effective backend range helper.
- Combat-style UI either does not claim to alter range/speed or demonstrably changes backend effective range/speed.

## Suggested First Fix

Start with the shared step validator and attack timer ordering. Those are small, high-confidence backend fixes that reduce disagreement between action masks, movement execution, and rendering. After that, fix combat approach to route to valid attack tiles around NPC footprints. That should directly improve the "pathing and combat movement does not feel right" issue before larger viewer event work.
