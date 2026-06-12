# Movement Audit Recommendations

## Goal

The Fight Caves simulator does not need to be a perfect OSRS server, but it should preserve the major mechanics that affect policy learning:

- movement legality should be consistent everywhere,
- attack range and line-of-sight should determine combat positioning,
- attack cadence should match the configured weapon speed,
- viewer behavior should not mislead manual evaluation,
- fixes should not add meaningful training overhead.

The recommendations below prioritize small backend correctness fixes that keep the environment fast and easy to roll back.

## Critical Fixes

### 1. Use One Shared Step Validator

Problem:

- Action masks, direct movement, route movement, BFS pathing, and NPC movement do not currently share one movement legality check.
- Diagonal movement can be legal in one path and illegal in another.

Recommendation:

- Add a shared `fc_can_step_size(state, x, y, size, dx, dy)` helper.
- Use it in:
  - action mask movement validity,
  - player direct movement,
  - route consumption,
  - BFS neighbor expansion,
  - NPC stepping.
- For diagonal movement, require:
  - destination footprint walkable,
  - horizontal side step passable,
  - vertical side step passable.

Expected cost:

- Very low. This is a constant-time tile check per attempted step.

Why it matters:

- This removes disagreement between training masks, backend execution, and viewer/debug movement.

### 2. Fix Run Diagonal And Intermediate-Step Validation

Problem:

- Low-level run diagonals are asymmetric.
- Run actions can validate only the final destination instead of both sequential steps.

Recommendation:

- Treat run as two sequential steps using the same step validator.
- Either:
  - make diagonal run equal to two repeated diagonal steps, or
  - remove low-level run diagonals and rely on route movement for run behavior.
- Validate and expose the intermediate tile.

Expected cost:

- Very low. Two step checks instead of one.

Why it matters:

- The policy should not learn skewed movement primitives.
- Manual movement should match route movement.

### 3. Fix Player Attack Timer Cadence

Problem:

- Player attack timer appears to be set and decremented in the same tick.
- A speed 5 weapon may effectively behave like speed 4 after attack tick processing.

Recommendation:

- Change timer ordering so a newly set attack timer is not decremented in the same tick.
- Add a focused test for player attack cadence.

Expected cost:

- No meaningful runtime cost.

Why it matters:

- DPS, Jad timings, projectile visuals, and learned combat behavior all depend on correct cadence.

### 4. Route Combat Approach To Attackable Tiles, Not NPC Centers

Problem:

- Combat auto-approach routes toward the NPC center, then trims the path.
- Large NPC centers can be unreachable or irrelevant.

Recommendation:

- Change combat BFS to stop when the player reaches a tile that satisfies:
  - in effective weapon range,
  - has line of sight/projectile path to the target footprint.
- Do not route to the NPC center first.
- Reuse the existing BFS structure; change the target predicate.

Expected cost:

- Low if recomputed only when needed.
- Avoid per-tick full recomputes unless target/LOS/route state changes.

Why it matters:

- This directly improves Jad and large-NPC combat movement without needing full OSRS pathfinding.

### 5. Make NPCs Chase Until Truly Attackable

Problem:

- NPC movement can stop when numeric distance is within attack range, even if LOS is blocked.

Recommendation:

- Change NPC arrival from `distance <= attack_range` to `npc_can_attack_player`.
- If an NPC is not truly attackable, it should continue moving.
- Keep movement greedy initially to avoid adding expensive NPC pathfinding.

Expected cost:

- Low. This adds a LOS/reachability check that already exists in nearby combat code.

Why it matters:

- Prevents passive NPC behavior behind obstacles and removes obvious geometry exploits.

## Important Viewer/Debug Fixes

### 6. Draw Debug Range From Backend Range

Problem:

- The active Twisted Bow backend range is 10, but one debug range overlay is hardcoded to 7.

Recommendation:

- Draw the range overlay from `state->player.weapon_range` or a future `fc_player_effective_weapon_range()` helper.

Expected cost:

- None for training.

Why it matters:

- Manual eval currently can look wrong even when backend range is correct.

### 7. Stop Letting Combat Style UI Misrepresent Backend Behavior

Problem:

- Accurate/Rapid/Longrange buttons do not appear to change backend speed or range.

Recommendation:

- Either:
  - make them visual/read-only for now, or
  - wire them through a single effective speed/range helper used by backend, UI, and debug overlay.

Expected cost:

- None if made read-only.
- Very low if wired through simple helpers.

Why it matters:

- Prevents manual testing from drawing wrong conclusions about max range or attack speed.

### 8. Event-Driven Viewer Rendering

Problem:

- Projectiles and attack animations are inferred from timers.
- Timer inference is brittle when backend timing changes.

Recommendation:

- Eventually emit small per-tick render events for:
  - player attack,
  - NPC attack,
  - projectile launch,
  - impact spotanim,
  - movement step.
- Viewer should consume events rather than guessing from cooldown timers.

Expected cost:

- Slightly more code, negligible training cost if event buffers are small and compiled into the core state.

Why it matters:

- This improves trust in visual eval, but it is less urgent than backend movement correctness.

## Defer For Now

These are not recommended as first-pass changes:

- Full OSRS directional collision asset replacement.
- Dynamic entity occupancy/blocking.
- Full NPC BFS/pathfinding every tick.
- OSRS-like run energy drain/regeneration.
- Player melee and magic combat modes.

Reason:

- They add complexity and potential overhead without being required for a close Fight Caves approximation.
- They can be revisited after the cheap correctness fixes are stable.

## Suggested Implementation Order

1. Add tests around current behavior before changing code:
   - diagonal blocked movement,
   - action mask vs execution agreement,
   - player attack cadence,
   - NPC LOS-blocked chase,
   - attack approach to large NPC perimeter.
2. Implement shared step validator and route/direct/NPC movement usage.
3. Fix run diagonal/intermediate-step handling.
4. Fix player attack timer cadence.
5. Change combat approach to use attackable-tile target predicate.
6. Change NPC movement to chase until truly attackable.
7. Fix debug range overlay and combat-style UI truthfulness.
8. Only after backend behavior is stable, add viewer render events.

## Rollback-Safe Implementation Plan

The safest way to avoid losing progress is to split this into small git checkpoints and keep every risky change behind a feature flag until tested.

### 1. Start From A Clean, Named Safety Point

Before implementation:

```bash
git status --short
git branch safety/movement-prework
git tag movement-prework-$(date +%Y%m%d-%H%M)
```

If there are uncommitted changes that should be preserved but not committed yet:

```bash
git stash push -u -m "movement-prework-local-state"
```

Only stash if the uncommitted changes are not needed for the movement work.

### 2. Use A Dedicated Work Branch

```bash
git checkout -b movement-correctness-phase1
```

Keep the branch local until the viewer and training smoke tests pass.

### 3. No-Commit Checkpoint Workflow

If commits are intentionally deferred until manual testing, use patch snapshots between each implementation chunk. This is safer than relying on memory or broad file restores.

Before the first code change:

```bash
mkdir -p /tmp/fc_movement_snapshots
git diff > /tmp/fc_movement_snapshots/00_before_movement_changes.patch
git ls-files --others --exclude-standard > /tmp/fc_movement_snapshots/00_untracked_files.txt
```

After each coherent chunk, write another snapshot:

```bash
git diff > /tmp/fc_movement_snapshots/01_shared_step_validator.patch
```

To abandon only the current chunk, restore the specific touched files rather than resetting the whole repo:

```bash
git restore path/to/file.c path/to/file.h
```

If local checkpoint commits are allowed later, they are safer and easier to revert than patch snapshots. Until then, keep changes small and test after each chunk.

### 4. Commit In Small Reversible Units After Testing Approval

Recommended commit sequence when commits are allowed:

1. `tests: add movement and attack cadence coverage`
2. `core: add shared step validator`
3. `core: use shared validator for player and route movement`
4. `core: use shared validator for npc movement`
5. `core: fix run intermediate-step validation`
6. `core: fix player attack timer cadence`
7. `core: route combat approach to attackable tiles`
8. `core: chase npc targets until attackable`
9. `viewer: align debug range with backend range`

Each commit should build and pass focused tests. If one change is bad, revert only that commit.

### 5. Prefer Runtime Feature Flags For Risky Behavior

For behavior that might affect training performance or policy dynamics, add compile-time or config-gated toggles:

- `fc_use_shared_step_validator`
- `fc_use_attackable_tile_approach`
- `fc_fix_player_attack_timer`
- `fc_npc_chase_until_attackable`

Default them on only after tests and viewer checks pass. If a training run regresses badly, flip the flag off to compare without reverting code.

Do not leave permanent flag clutter forever. Once a behavior is accepted, remove the old path in a follow-up cleanup commit.

### 6. Keep A Fast Validation Loop

After each backend commit, run:

```bash
./runescape-rl/train.sh test
timeout --foreground 10s ./runescape-rl/train.sh
python3 demo-env/eval_viewer.py --ckpt <known-good-checkpoint>
```

The short training run is not a quality signal, but it catches backend build/runtime regressions quickly.

### 7. Measure Speed Before And After

Before changing movement:

```bash
timeout --foreground 30s ./runescape-rl/train.sh
```

Record approximate SPS/env throughput. Repeat after each risky backend change. If throughput drops meaningfully, keep the change disabled behind its flag until optimized.

### 8. Rollback Options

If a single commit is bad:

```bash
git revert <commit_sha>
```

If the whole branch is bad:

```bash
git checkout main
git branch movement-correctness-phase1-bad-backup movement-correctness-phase1
```

Then continue from `main` or from `safety/movement-prework`.

If uncommitted work gets messy:

```bash
git diff > /tmp/movement_wip.patch
git restore <specific_files>
```

Avoid broad destructive resets unless the target commit and saved patch are both verified.

## Recommended First PR/Commit Scope

The first implementation batch should be:

- tests for movement legality and attack cadence,
- shared step validator,
- action mask/direct movement/route movement using that validator,
- debug range overlay fixed to backend range.

This gives immediate correctness wins with almost no performance risk. Combat approach and NPC LOS chase should be the second batch because they change learned behavior more directly.
