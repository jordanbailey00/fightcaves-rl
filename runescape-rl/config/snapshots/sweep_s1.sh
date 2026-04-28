#!/bin/bash
# v28.8 Sweep 1 — reward leave-one-out ablation.
#
# 10 runs, each setting exactly one of the following env rewards to 0
# while keeping all else identical to the v28.8 baseline. Goal: determine
# which rewards are actually contributing so we can prune the config
# before running Sweep 2 (non-reward hparams) and Sweep 3 (reward hparams).
#
# Each run is 3B steps (v28.8 budget). Total wall time ~10h at typical
# throughput. Runs sequentially via train.sh with CONFIG_PATH override.
#
# Usage:
#   bash sweep_s1.sh                 # run all 10 variants in order
#   bash sweep_s1.sh 1               # run only s1.01
#   bash sweep_s1.sh 3 7             # run s1.03 then s1.07
#   bash sweep_s1.sh 1 2 3 4 5       # run first half
#
# Each run is tagged in wandb as v28.8-s1.NN automatically via the
# WANDB_TAG env var (train.sh forwards it as --tag to pufferl).

set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

declare -A DESCRIPTIONS=(
    [1]="s1.01  zero w_npc_kill                      (was 3.5)"
    [2]="s1.02  zero w_invalid_action                (was -0.1)"
    [3]="s1.03  zero w_tick_penalty                  (was -0.005)"
    [4]="s1.04  zero shape_wasted_attack_penalty     (was -0.1)"
    [5]="s1.05  zero shape_wrong_prayer_penalty      (was -0.3)"
    [6]="s1.06  zero shape_unnecessary_prayer_penalty (was -0.2)"
    [7]="s1.07  zero shape_food_full_waste_penalty   (was -6.5)"
    [8]="s1.08  zero shape_food_waste_scale          (was -1.2)"
    [9]="s1.09  zero shape_pot_full_waste_penalty    (was -6.5)"
    [10]="s1.10 zero shape_pot_waste_scale           (was -1.2)"
)

# Determine which variants to run
if [ $# -eq 0 ]; then
    TO_RUN=(1 2 3 4 5 6 7 8 9 10)
else
    TO_RUN=("$@")
fi

# Validate requested indices exist
for idx in "${TO_RUN[@]}"; do
    PADDED=$(printf "%02d" "$idx")
    CONFIG_FILE="$SRC_DIR/config/fight_caves_s1_${PADDED}.ini"
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "[sweep_s1] ERROR: config not found for variant $idx at $CONFIG_FILE"
        exit 1
    fi
done

echo "[sweep_s1] Sweep plan:"
for idx in "${TO_RUN[@]}"; do
    echo "  - ${DESCRIPTIONS[$idx]}"
done
echo ""
echo "[sweep_s1] Starting in 3s (Ctrl-C to abort)..."
sleep 3

TOTAL=${#TO_RUN[@]}
COUNT=0
for idx in "${TO_RUN[@]}"; do
    COUNT=$((COUNT + 1))
    PADDED=$(printf "%02d" "$idx")
    CONFIG_FILE="$SRC_DIR/config/fight_caves_s1_${PADDED}.ini"
    VARIANT_TAG="v28.8-s1.${PADDED}"

    echo ""
    echo "========================================================"
    echo "[sweep_s1] ($COUNT/$TOTAL) ${DESCRIPTIONS[$idx]}"
    echo "[sweep_s1] Config:  $CONFIG_FILE"
    echo "[sweep_s1] Tag:     $VARIANT_TAG"
    echo "[sweep_s1] Started: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================================"

    WANDB_TAG="$VARIANT_TAG" CONFIG_PATH="$CONFIG_FILE" bash "$SRC_DIR/train.sh"

    echo ""
    echo "[sweep_s1] ($COUNT/$TOTAL) Finished ${DESCRIPTIONS[$idx]} at $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
done

echo "========================================================"
echo "[sweep_s1] Sweep complete. Ran $TOTAL variant(s)."
echo "========================================================"
