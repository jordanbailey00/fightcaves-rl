#!/bin/bash
# rerun_v27_series.sh — serially rerun v27, v27.1, v27.2 with the new rwd_*
# analytics backend. Each run uses the corresponding snapshot config from
# runescape-rl/config/snapshots/ and is tagged `v27*-rerun` in W&B so it
# doesn't collide with the original runs.
#
# Usage (from anywhere):
#   bash /home/joe/projects/runescape-rl/claude/runescape-rl/scripts/rerun_v27_series.sh
#
# Each run takes ~30 min at 3.5B steps, so total wall clock is ~90 min.
# On exit (clean or failure), the active config is restored to v27.3.

set -euo pipefail

BASE="/home/joe/projects/runescape-rl/claude"
SRC="$BASE/runescape-rl"
CONFIG="$SRC/config/fight_caves.ini"
SNAPSHOTS="$SRC/config/snapshots"

# Restore v27.3 config on exit (normal or error)
restore_v27_3() {
    if [ -f "$SNAPSHOTS/v27.3.ini" ]; then
        cp "$SNAPSHOTS/v27.3.ini" "$CONFIG"
        echo "[rerun] Restored v27.3 config to $CONFIG"
    fi
}
trap restore_v27_3 EXIT

# Sanity checks
for v in v27 v27.1 v27.2 v27.3; do
    if [ ! -f "$SNAPSHOTS/$v.ini" ]; then
        echo "[rerun] ERROR: snapshot $SNAPSHOTS/$v.ini is missing" >&2
        exit 1
    fi
done

for version in v27 v27.1 v27.2; do
    GROUP="${version}-rerun"
    TAG="${version}-rerun"

    echo ""
    echo "============================================================"
    echo "[rerun] Starting $version ($GROUP)"
    echo "============================================================"
    echo "[rerun] Swapping config to $SNAPSHOTS/$version.ini"
    cp "$SNAPSHOTS/$version.ini" "$CONFIG"
    grep -E "^w_wave_clear|^w_jad_kill|^w_correct_jad_prayer|^w_correct_danger_prayer" "$CONFIG"
    echo ""

    # Launch training; train.sh will sync to pufferlib_4 copy, rebuild backend if
    # needed, and block until the run completes.
    bash "$SRC/train.sh" --wandb-group "$GROUP" --tag "$TAG"

    echo ""
    echo "[rerun] Finished $version"
done

echo ""
echo "[rerun] All 3 reruns complete (v27-rerun, v27.1-rerun, v27.2-rerun)"
echo "[rerun] Analyze with: rerun groups in W&B under jbailey8531-oakton-college/fight caves rl"
