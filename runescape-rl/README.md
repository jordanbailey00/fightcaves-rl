# Fight Caves RL

Training a reinforcement learning agent to complete the Old School RuneScape Fight Caves — a 63-wave PvM gauntlet ending in a boss fight against TzTok-Jad.

**Goals:**
- Build a deterministic, high-performance Fight Caves simulation in C
- Train a PPO agent from scratch to clear all 63 waves and defeat Jad
- Achieve this without human demonstrations or hardcoded strategies

**Current results (v28.8 / v29.1 / v30.0):**
- Agent reaches wave 63 (Jad) on **97.3% of episodes** from cold start
- Learned safespotting, prayer switching, kiting, and resource management
- **Jad kill rate: 59.7% peak (v28.8), 30.2% final (v29.1 SOTA)** — full Jad-kill capability
- Cold-start training: ~26 min for 3B steps on RTX 5070 Ti (~1.9M SPS)
- Recent diagnostic runs added corrected Jad healer respawn behavior and a no-consumables ablation:
  - **v35 (`8z4lqldl`)** — healer respawn correction, peak `jad_kill_rate=96.5%`, best saved checkpoint `90.6%`
  - **v36 (`jta3lkgx`)** — `initial_sharks=0`, `initial_prayer_doses=0`, no action-space change, peak `jad_kill_rate=80.8%`, best saved checkpoint `69.1%`

![Agent Demo](runescape-rl/assets/demo.gif)

---

## Getting Started

### Requirements

- Linux (tested on Ubuntu 24.04)
- Python 3.12+
- NVIDIA GPU with CUDA 12.8+ and cuDNN 9+
- CMake 3.20+
- GCC/G++
- Raylib 5.5 (vendored in `demo-env/raylib/`)

### Clone & Setup

```bash
git clone https://github.com/jordanbailey00/fight-caves-rl.git
cd fight-caves-rl/runescape-rl

# Create virtual environment
python3 -m venv .venv
source .venv/bin/activate

# Install dependencies
pip install torch numpy wandb pybind11 rich rich-argparse
```

### Play (Viewer)

Build and launch the interactive Raylib viewer:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/demo-env/fc_viewer
```

Controls:
- Arrow keys / WASD — move player
- 1/2/3 — toggle protect melee/range/magic
- Right-click drag — orbit camera
- Scroll — zoom
- O — toggle debug overlay
- Q/Esc — quit

<!-- TODO: Screenshot of the playable viewer with HUD, prayer icons, and NPC health bars -->

### Train

Run a full training session (cold start, ~26 min for 3B steps on RTX 5070 Ti):

```bash
FORCE_BACKEND_REBUILD=1 bash train.sh
```

`train.sh` handles venv activation, config sync, backend compilation, and launch. Training logs to W&B by default. Tag a run with the `WANDB_TAG` env var:

```bash
WANDB_TAG=v30.1 bash train.sh
```

Key flags:
```bash
bash train.sh --no-wandb                    # disable W&B logging
bash train.sh sweep                          # run hyperparameter sweep
LOAD_MODEL_PATH=/path/to/checkpoint.bin bash train.sh  # warm-start
```

### Evaluate a Checkpoint

Watch a trained policy play in the viewer:

```bash
source .venv/bin/activate
python3 demo-env/eval_viewer.py --ckpt /path/to/checkpoint.bin
```

---

## Commands

| Command | Description |
|---------|-------------|
| `bash train.sh` | Train with current config (cold start) |
| `bash train.sh sweep` | Run Protein hyperparameter sweep |
| `bash train.sh --no-wandb` | Train without W&B logging |
| `LOAD_MODEL_PATH=<path> bash train.sh` | Warm-start from checkpoint |
| `./build/demo-env/fc_viewer` | Launch playable viewer |
| `python3 demo-env/eval_viewer.py --ckpt <path>` | Replay trained policy |
| `bash analyze_run.sh <run_id>` | Quick W&B run analysis |

---

## Architecture

<!-- TODO: Simple block diagram showing fc-core -> training-env -> PufferLib and fc-core -> demo-env -> Raylib -->

```
runescape-rl/
├── fc-core/           Deterministic C game simulation
│   ├── include/       Headers (types, contracts, combat, reward, API)
│   └── src/           Implementation (tick, state, combat, NPC, wave, prayer)
├── training-env/      PufferLib 4.0 adapter (binding.c, fight_caves.h)
├── demo-env/          Raylib 3D viewer + eval tooling
│   ├── src/           Viewer, debug overlay, asset loaders
│   └── assets/        Collision map, prayer/item sprites
├── config/            Training config (fight_caves.ini)
└── docs/              Run history, RL config reference, design doc
```

**Backend (`fc-core/`):** Pure C, zero allocations per tick. Deterministic game simulation — combat, pathfinding, prayer, waves, NPC AI. Both the viewer and trainer call into the same `fc_step()` function.

**Training (`training-env/`):** PufferLib 4.0 wrapper. Compiles the C backend into a shared library with CUDA-accelerated vectorized environments. 4096 parallel agents, ~2M steps/sec on a single GPU.

**Viewer (`demo-env/`):** Raylib-based 3D viewer for human play and policy replay. Debug overlay shows reward breakdown, NPC stats, prayer state, threat context.

**Training stack:** PPO with MinGRU policy (2-layer, 256 hidden, ~439K params). W&B integration for logging and sweep analysis.

---

## RL Config

Current live config (v30.0 — v28.8 baseline with dead full-waste keys removed):

| Category | Key params |
|----------|-----------|
| **Combat rewards** | `w_damage_dealt=0.9`, `w_npc_kill=3.5`, `w_wave_clear=15.0`, `w_jad_kill=2000.0` |
| **Survival penalties** | `w_damage_taken=-1.9` (squared, 70x amplified), `w_player_death=-11.0` |
| **Prayer shaping** | `w_correct_danger_prayer=0.25`, `w_correct_jad_prayer=1.5`, `shape_wrong_prayer_penalty=-0.3`, `shape_unnecessary_prayer_penalty=-0.2` |
| **Positioning** | `shape_npc_melee_penalty=-0.8`, `shape_safespot_attack_reward=1.5`, `shape_kiting_reward=2.2` (band 2-10) |
| **Resource management** | `shape_food_waste_scale=-1.2`, `shape_pot_waste_scale=-1.2` (proportional waste only) |
| **Wave-stall pressure** | `shape_wave_stall_start=1400`, `shape_wave_stall_base_penalty=-0.5`, `shape_wave_stall_cap=-2.0` |
| **Procedural penalties** | `w_invalid_action=-0.1`, `w_tick_penalty=-0.005`, `shape_wasted_attack_penalty=-0.1` |
| **PPO** | `lr=3e-4`, `gamma=0.999`, `gae_lambda=0.95`, `ent_coef=0.01`, `horizon=256`, `minibatch=4096`, `total_timesteps=3B` |
| **Policy** | MinGRU, 2 layers, 256 hidden (~439K params) |
| **Vector** | `4096 agents`, `2 buffers` |

Full config reference: [`docs/rl_config.md`](docs/rl_config.md). Full sweep history with deltas and deployment SOTA: [`docs/sweep_history.md`](docs/sweep_history.md).

---

## Results

### Deployment SOTA: v29.1 (`ml71cg6v` / autumn-bush-330)

v29.1 disables prioritized experience replay (`prio_alpha=0`) over the v28.8 reward config. Best deployment artifact at the 1.62B checkpoint.

| Metric | v28.8 (prior SOTA) | **v29.1 (current SOTA)** | Δ |
|--------|---|---|---|
| peak `jad_kill_rate` | 0.591 | **0.598** | +1.2% |
| peak step | 1.80B | **1.56B** | -240M (-13% compute) |
| best 250M window | 0.316 | **0.336** | +6.3% |
| final `jad_kill_rate` | 0.223 | **0.281** | +26% |
| final `reached_wave_63` | 0.973 | 0.930 | -4.4% |
| final conditional Jad kill | 22.9% | **30.2%** | +32% |
| training time | 26 min (3B steps) | 26 min (3B steps) | — |
| throughput | 1.92M SPS | 1.92M SPS | — |

### Key Milestones

- **v21.2** — First cold start to reach wave 60 (range-7 weapon, no LOS)
- **v22.1** — First Jad kills (1.5%, warm-started), introduced TBow combat model
- **v25.9** — First cold start under TBow+LOS to reach wave 60+
- **v28.4** — 20-50% Jad kill rate at 3.5B budget
- **v28.8** — 59.1% peak Jad kill rate, 22.3% final (prior deployment SOTA, 3B budget)
- **v29.1** — `prio_alpha=0` ablation: 33% compute reduction at same-or-better deployment quality (current SOTA)
- **v30.0** — v28.8 baseline with dead `*_full_waste_penalty` keys removed; verified byte-identical to v28.8
- **v35** — Corrected Jad healer respawn semantics: healers only re-arm after Jad is fully healed
- **v36** — No-consumables diagnostic: removed starting food and prayer doses while keeping the same action heads and masks

### Active: 3-Sweep Pruning Plan

The agent now reliably kills Jad. Current focus is pruning the reward config and tuning hparams via three sequential sweeps:

1. **Sweep 1 (complete)** — leave-one-out reward ablation (10 runs). Identified 2 dead-code keys (full-waste penalties) for removal and `w_npc_kill` as over-penalizing (peak +0.21 when zeroed).
2. **Sweep 2 (planned)** — non-reward hparam sweep (Protein/paretosweep over LR, ent_coef, clip, gamma, prio_alpha, etc.).
3. **Sweep 3 (planned)** — reward magnitude sweep on surviving keys.

Full experiment history: [`docs/sweep_history.md`](docs/sweep_history.md).

---

## Project Status

Active research. The agent reaches Jad on 97% of episodes and kills it at 59.7% peak rate. Current focus is reward and hparam pruning via sequential sweeps to find a tighter, deployment-ready config.

Full experiment history with configs, results, and analysis:
- [`docs/sweep_history.md`](docs/sweep_history.md) — sweep results, deltas, SOTA tracking (current)
- [`docs/run_history.md`](docs/run_history.md) — chronological run log (legacy)

---

## License

MIT License. See [LICENSE](../LICENSE) for details.
