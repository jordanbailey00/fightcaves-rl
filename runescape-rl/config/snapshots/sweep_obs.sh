#!/bin/bash
# OBS Sweep / Ablation — single-feature obs cuts from v30.0 / v28.8 baseline.
#
# Runs each variant sequentially via train.sh with CONFIG_PATH override.
# Each run ~26 min at 3B steps. Full sweep ~80 min wall time.
#
# Usage:
#   bash sweep_obs.sh         # run all 3 variants in order (0 → 2)
#   bash sweep_obs.sh 0       # run only obs.0
#   bash sweep_obs.sh 0 2     # run obs.0 then obs.2
#
# Baseline reference run (no rerun needed):
#   v30.0 (bqrdh8hc) — 3B steps, byte-identical to v28.8.
#
# Each run is tagged in wandb (single tag) so the ablation is identifiable
# without cross-referencing docs:
#   obs.0_no_distance     ablate NPC_DISTANCE
#   obs.1_no_aggregates   ablate the 9 incoming-hit aggregates
#   obs.2_no_valid        ablate NPC_VALID
# Tag flow: WANDB_TAG env var → train.sh → --tag → pufferl.py:246
# wandb.init(tags=[args['tag']]).
#
# All three variants flip exactly one obs_ablate_* flag in [env].
# Backend wiring: binding.c reads kwarg → FightCaves.obs_ablate_* →
# fc_apply_obs_ablation() in fc-core/src/fc_state.c zeroes the slots
# AFTER fc_write_obs runs. Reward features and action mask are untouched.

set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

declare -A DESCRIPTIONS=(
    [0]="obs.0  ablate NPC_DISTANCE             (8 floats zeroed)"
    [1]="obs.1  ablate incoming-hit aggregates  (9 floats zeroed: PLAYER_IN_*_1T/2T + META_IN_*_3T)"
    [2]="obs.2  ablate NPC_VALID                (8 floats zeroed)"
)

# wandb tag per variant (passed via --tag → wandb.init(tags=[...]) in pufferl.py:246).
# Self-describing so runs are identifiable in W&B without cross-referencing docs.
declare -A WANDB_TAGS=(
    [0]="obs.0_no_distance"
    [1]="obs.1_no_aggregates"
    [2]="obs.2_no_valid"
)

# Determine which variants to run
if [ $# -eq 0 ]; then
    TO_RUN=(0 1 2)
else
    TO_RUN=("$@")
fi

# Validate requested indices exist
for idx in "${TO_RUN[@]}"; do
    CONFIG_FILE="$SRC_DIR/config/fight_caves_obs${idx}.ini"
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "[sweep_obs] ERROR: config not found for variant $idx at $CONFIG_FILE"
        exit 1
    fi
done

echo "[sweep_obs] Sweep plan:"
for idx in "${TO_RUN[@]}"; do
    echo "  - ${DESCRIPTIONS[$idx]}"
done
echo ""
echo "[sweep_obs] Baseline = v30.0 (bqrdh8hc / v28.8 reward config)."
echo "[sweep_obs] Starting in 3s (Ctrl-C to abort)..."
sleep 3

TOTAL=${#TO_RUN[@]}
COUNT=0
for idx in "${TO_RUN[@]}"; do
    COUNT=$((COUNT + 1))
    CONFIG_FILE="$SRC_DIR/config/fight_caves_obs${idx}.ini"
    VARIANT_TAG="${WANDB_TAGS[$idx]}"

    echo ""
    echo "========================================================"
    echo "[sweep_obs] ($COUNT/$TOTAL) ${DESCRIPTIONS[$idx]}"
    echo "[sweep_obs] Config:    $CONFIG_FILE"
    echo "[sweep_obs] WANDB tag: $VARIANT_TAG"
    echo "[sweep_obs] Started:   $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================================"

    WANDB_TAG="$VARIANT_TAG" CONFIG_PATH="$CONFIG_FILE" bash "$SRC_DIR/train.sh"

    echo ""
    echo "[sweep_obs] ($COUNT/$TOTAL) Finished ${DESCRIPTIONS[$idx]} at $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
done

echo "========================================================"
echo "[sweep_obs] Sweep complete. Ran $TOTAL variant(s)."
echo "========================================================"
