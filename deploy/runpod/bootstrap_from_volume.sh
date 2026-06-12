#!/usr/bin/env bash
set -euo pipefail

VOLUME_ROOT="${RUNPOD_VOLUME_ROOT:-/runpod-volume}"
WORK_ROOT="${RUNPOD_WORK_ROOT:-/workspace/fight-caves-rl}"
VENV_ROOT="${RUNPOD_VENV_ROOT:-/opt/runescape-rl-venv}"

ARCHIVE_PATH="${VOLUME_ROOT}/runescape-rl/releases/runescape-rl-v24s-training.tar.gz"
STAGED_WARM_CKPT="${VOLUME_ROOT}/runescape-rl/checkpoints/fight_caves/u58coupx/0000001311768576.bin"

RUNTIME_ROOT="${VOLUME_ROOT}/runescape-rl/runtime"
RUNTIME_CKPTS="${RUNTIME_ROOT}/checkpoints"
RUNTIME_LOGS="${RUNTIME_ROOT}/logs"
RUNTIME_WANDB="${RUNTIME_ROOT}/wandb"
RUNTIME_WARM_DIR="${RUNTIME_CKPTS}/fight_caves/u58coupx"
RUNTIME_WARM_CKPT="${RUNTIME_WARM_DIR}/0000001311768576.bin"

if [ ! -f "${ARCHIVE_PATH}" ]; then
    echo "Missing training archive: ${ARCHIVE_PATH}" >&2
    exit 1
fi

if [ ! -f "${STAGED_WARM_CKPT}" ]; then
    echo "Missing staged warm checkpoint: ${STAGED_WARM_CKPT}" >&2
    exit 1
fi

if [ ! -x "${VENV_ROOT}/bin/python" ]; then
    runpod-install-runtime
fi

mkdir -p "${RUNTIME_WARM_DIR}" "${RUNTIME_LOGS}/fight_caves" "${RUNTIME_WANDB}"
if [ ! -f "${RUNTIME_WARM_CKPT}" ]; then
    cp "${STAGED_WARM_CKPT}" "${RUNTIME_WARM_CKPT}"
fi

rm -rf "${WORK_ROOT}"
mkdir -p "${WORK_ROOT}"
tar -xzf "${ARCHIVE_PATH}" -C "${WORK_ROOT}"

rm -rf "${WORK_ROOT}/runescape-rl/.venv"
ln -s "${VENV_ROOT}" "${WORK_ROOT}/runescape-rl/.venv"

rm -rf "${WORK_ROOT}/pufferlib_4/checkpoints" "${WORK_ROOT}/pufferlib_4/logs" "${WORK_ROOT}/pufferlib_4/wandb"
ln -s "${RUNTIME_CKPTS}" "${WORK_ROOT}/pufferlib_4/checkpoints"
ln -s "${RUNTIME_LOGS}" "${WORK_ROOT}/pufferlib_4/logs"
ln -s "${RUNTIME_WANDB}" "${WORK_ROOT}/pufferlib_4/wandb"

cat <<EOF
Workspace restored to: ${WORK_ROOT}
Volume root: ${VOLUME_ROOT}
Python env: ${VENV_ROOT}
Warm checkpoint: ${RUNTIME_WARM_CKPT}

Next:
  cd ${WORK_ROOT}/runescape-rl
  runpod-verify
  FORCE_BACKEND_REBUILD=1 LOAD_MODEL_PATH=${RUNTIME_WARM_CKPT} bash train.sh
EOF
