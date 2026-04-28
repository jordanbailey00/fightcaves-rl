#!/bin/bash
# v29.x ablation sweep — single-knob variations from v28.9 baseline.
#
# Runs each variant sequentially via train.sh with CONFIG_PATH override.
# Each run ~25 min at 2.5B steps. Full sweep ~2h wall time.
#
# Usage:
#   bash sweep_v29.sh           # run all variants in order (0 → 4)
#   bash sweep_v29.sh 0         # run only v29.0
#   bash sweep_v29.sh 0 1       # run v29.0 then v29.1
#   bash sweep_v29.sh 1 2 3 4   # skip v29.0, run the other four
#
# Each run is tagged in wandb as v29.<idx> automatically via the
# WANDB_TAG env var (which train.sh forwards as --tag to the trainer,
# which pufferl.py passes to wandb.init). No manual tagging needed.

set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

declare -A DESCRIPTIONS=(
    [0]="v29.0  noise-floor probe       (seed=7)"
    [1]="v29.1  disable prioritized replay (prio_alpha=0)"
    [2]="v29.2  lower entropy bonus       (ent_coef=0.005)"
    [3]="v29.3  enable LR annealing       (anneal_lr=1, min_lr_ratio=0.1)"
    [4]="v29.4  smaller policy net        (hidden_size=192)"
)

# Determine which variants to run
if [ $# -eq 0 ]; then
    TO_RUN=(0 1 2 3 4)
else
    TO_RUN=("$@")
fi

# Validate requested indices exist
for idx in "${TO_RUN[@]}"; do
    CONFIG_FILE="$SRC_DIR/config/fight_caves_v29_${idx}.ini"
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "[sweep_v29] ERROR: config not found for variant $idx at $CONFIG_FILE"
        exit 1
    fi
done

echo "[sweep_v29] Sweep plan:"
for idx in "${TO_RUN[@]}"; do
    echo "  - ${DESCRIPTIONS[$idx]}"
done
echo ""
echo "[sweep_v29] Starting in 3s (Ctrl-C to abort)..."
sleep 3

TOTAL=${#TO_RUN[@]}
COUNT=0
for idx in "${TO_RUN[@]}"; do
    COUNT=$((COUNT + 1))
    CONFIG_FILE="$SRC_DIR/config/fight_caves_v29_${idx}.ini"
    VARIANT_TAG="v29.${idx}"

    echo ""
    echo "========================================================"
    echo "[sweep_v29] ($COUNT/$TOTAL) ${DESCRIPTIONS[$idx]}"
    echo "[sweep_v29] Config:  $CONFIG_FILE"
    echo "[sweep_v29] Started: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================================"

    WANDB_TAG="$VARIANT_TAG" CONFIG_PATH="$CONFIG_FILE" bash "$SRC_DIR/train.sh"

    echo ""
    echo "[sweep_v29] ($COUNT/$TOTAL) Finished ${DESCRIPTIONS[$idx]} at $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
done

echo "========================================================"
echo "[sweep_v29] Sweep complete. Ran $TOTAL variant(s)."
echo "========================================================"
