#!/bin/bash
# Train Fight Caves RL agent with wandb logging.
# Usage: ./train.sh [--no-wandb]
# Optional:
#   LOAD_MODEL_PATH=/path/to/checkpoint.bin ./train.sh
#   LOAD_MODEL_PATH=latest ./train.sh

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SRC_DIR/.." && pwd)"
PUFFER_DIR="${PUFFER_DIR:-$ROOT_DIR/pufferlib_4}"
CONFIG_PATH="${CONFIG_PATH:-$SRC_DIR/config/fight_caves.ini}"
TRAINING_BUILD_SH="$SRC_DIR/training-env/build.sh"

if [ ! -d "$PUFFER_DIR" ]; then
    echo "Error: PufferLib not found at $PUFFER_DIR"
    exit 1
fi

if [ ! -f "$CONFIG_PATH" ]; then
    echo "Error: config not found at $CONFIG_PATH"
    exit 1
fi

if [ ! -f "$TRAINING_BUILD_SH" ]; then
    echo "Error: local training backend build script not found at $TRAINING_BUILD_SH"
    exit 1
fi

# Sync config to where PufferLib reads it
cp "$CONFIG_PATH" "$PUFFER_DIR/config/fight_caves.ini"
echo "[train.sh] Synced config from $CONFIG_PATH to $PUFFER_DIR/config/fight_caves.ini"

mkdir -p \
    "$PUFFER_DIR/checkpoints/fight_caves" \
    "$PUFFER_DIR/logs/fight_caves" \
    "$PUFFER_DIR/wandb"

cd "$PUFFER_DIR"
VENV_DIR="$SRC_DIR/.venv"
if [ -z "${PYTHON_BIN:-}" ]; then
    if [ -x "$VENV_DIR/bin/python3" ]; then
        PYTHON_BIN="$VENV_DIR/bin/python3"
    elif [ -x "$VENV_DIR/bin/python" ]; then
        PYTHON_BIN="$VENV_DIR/bin/python"
    elif command -v python3 >/dev/null 2>&1; then
        PYTHON_BIN="$(command -v python3)"
    elif command -v python >/dev/null 2>&1; then
        PYTHON_BIN="$(command -v python)"
    else
        echo "Error: python not found. Create $VENV_DIR or install python3." >&2
        exit 1
    fi
fi
export PYTHON_BIN
export VIRTUAL_ENV="$VENV_DIR"
export PUFFERLIB_DIR="$PUFFER_DIR"
export PATH="$VENV_DIR/bin:/usr/local/cuda/bin:$PATH"
export FC_COLLISION_PATH="$SRC_DIR/fc-core/assets/fightcaves.collision"
export WANDB_DIR="$PUFFER_DIR/wandb"
export WANDB_CACHE_DIR="$PUFFER_DIR/wandb/.cache"
export WANDB_CONFIG_DIR="$PUFFER_DIR/wandb/.config"
export WANDB_DATA_DIR="$PUFFER_DIR/wandb/.data"
export WANDB_PROJECT="${WANDB_PROJECT:-fight caves rl}"
CUDNN_LIB="$("$PYTHON_BIN" -c "import nvidia.cudnn, os; print(os.path.join(nvidia.cudnn.__path__[0], 'lib'))" 2>/dev/null || true)"
if [ -n "$CUDNN_LIB" ]; then
    export LD_LIBRARY_PATH="$CUDNN_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export CC="${CC:-gcc}"
export CXX="${CXX:-g++}"

EXT_SUFFIX="$("$PYTHON_BIN" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or '')")"
BACKEND_SO="$PUFFER_DIR/pufferlib/_C${EXT_SUFFIX}"
BACKEND_REBUILD_REASON=""
if [ ! -f "$BACKEND_SO" ]; then
    BACKEND_REBUILD_REASON="missing backend"
elif ! "$PYTHON_BIN" -c "import importlib.util, sys; spec=importlib.util.spec_from_file_location('pufferlib._C', sys.argv[1]); mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod); ok=(getattr(mod, 'env_name', None) == 'fight_caves' and hasattr(mod, 'create_pufferl')); raise SystemExit(0 if ok else 1)" "$BACKEND_SO"; then
    BACKEND_REBUILD_REASON="backend missing compiled trainer API"
elif find "$SRC_DIR/fc-core" "$SRC_DIR/training-env" "$PUFFER_DIR/src/vecenv.h" -type f -newer "$BACKEND_SO" -print -quit | grep -q .; then
    BACKEND_REBUILD_REASON="backend sources newer than compiled extension"
elif [ "${FORCE_BACKEND_REBUILD:-0}" = "1" ]; then
    BACKEND_REBUILD_REASON="FORCE_BACKEND_REBUILD=1"
fi

if [ -n "$BACKEND_REBUILD_REASON" ]; then
    echo "[train.sh] Rebuilding fight_caves backend: $BACKEND_REBUILD_REASON"
    bash "$TRAINING_BUILD_SH"
fi

MODE="train"
if [ "$#" -gt 0 ]; then
    case "$1" in
        train|eval|sweep|paretosweep)
            MODE="$1"
            shift
            ;;
    esac
fi

WANDB_FLAG="--wandb"
EXTRA_ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--no-wandb" ]; then
        WANDB_FLAG=""
    else
        EXTRA_ARGS+=("$arg")
    fi
done
CMD=("$PYTHON_BIN" -m pufferlib.pufferl "$MODE" fight_caves)
if [ -n "$WANDB_FLAG" ]; then
    CMD+=("$WANDB_FLAG")
fi
CMD+=(--wandb-project "$WANDB_PROJECT")
if [ -n "${WANDB_TAG:-}" ]; then
    CMD+=(--tag "$WANDB_TAG")
fi
if [ -n "${LOAD_MODEL_PATH:-}" ]; then
    echo "[train.sh] Using warm-start checkpoint: $LOAD_MODEL_PATH"
    CMD+=(--load-model-path "$LOAD_MODEL_PATH")
fi
if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
    CMD+=("${EXTRA_ARGS[@]}")
fi

"${CMD[@]}"
