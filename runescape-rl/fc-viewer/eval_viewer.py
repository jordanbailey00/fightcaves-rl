#!/usr/bin/env python3
"""
eval_viewer.py — Watch a trained policy play in the full debug viewer.

Uses PufferLib's CUDA backend for policy inference, pipes actions to the
standalone viewer (fc_viewer --policy-pipe) via stdin/stdout.

Usage:
    python fc-viewer/eval_viewer.py --ckpt <path_to_.bin>
    python fc-viewer/eval_viewer.py --ckpt latest

Controls (in viewer window):
    Space       — pause/resume
    Side panel  — replay TPS presets
    Up/Down     — cycle replay speed presets
    Right-drag  — orbit camera
    Scroll      — zoom
    Shift+4/5   — camera presets
    O           — debug overlays
    Q/Esc       — quit
"""

import argparse
import ast
import glob
import os
import re
import subprocess
import sys

import numpy as np


def script_dir():
    return os.path.dirname(os.path.abspath(__file__))


def repo_root():
    return os.path.dirname(script_dir())


def workspace_root():
    return os.path.dirname(repo_root())


def ensure_local_pufferlib_on_path():
    default_puffer_dir = os.path.join(workspace_root(), "pufferlib_4")
    puffer_dir = os.environ.get("PUFFERLIB_DIR", default_puffer_dir)
    if os.path.isdir(puffer_dir) and puffer_dir not in sys.path:
        sys.path.insert(0, puffer_dir)
    return puffer_dir


def _safe_eval_macro(expr, raw_defs, values):
    node = ast.parse(expr, mode="eval")

    def eval_node(n):
        if isinstance(n, ast.Expression):
            return eval_node(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float)):
            return int(n.value)
        if isinstance(n, ast.Name):
            return resolve_macro(n.id, raw_defs, values)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.UAdd, ast.USub)):
            val = eval_node(n.operand)
            return val if isinstance(n.op, ast.UAdd) else -val
        if isinstance(n, ast.BinOp) and isinstance(
            n.op, (ast.Add, ast.Sub, ast.Mult, ast.Div, ast.FloorDiv, ast.Mod)
        ):
            left = eval_node(n.left)
            right = eval_node(n.right)
            if isinstance(n.op, ast.Add):
                return left + right
            if isinstance(n.op, ast.Sub):
                return left - right
            if isinstance(n.op, ast.Mult):
                return left * right
            if isinstance(n.op, (ast.Div, ast.FloorDiv)):
                return left // right
            return left % right
        raise RuntimeError(f"Unsupported macro expression: {expr}")

    return int(eval_node(node))


def resolve_macro(name, raw_defs, values):
    if name in values:
        return values[name]
    if name not in raw_defs:
        raise RuntimeError(f"Missing macro definition: {name}")
    values[name] = _safe_eval_macro(raw_defs[name], raw_defs, values)
    return values[name]


def parse_header_constants(path, names, base_values=None):
    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    raw_defs = {}
    define_re = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
    for line in lines:
        match = define_re.match(line)
        if not match:
            continue
        name, body = match.groups()
        body = re.sub(r"/\*.*?\*/", "", body).split("//", 1)[0].strip()
        if body:
            raw_defs[name] = body

    values = dict(base_values or {})
    for name in names:
        values[name] = resolve_macro(name, raw_defs, values)
    return values


def load_contract_dims():
    repo = repo_root()
    shared_header = os.path.join(repo, "fc-core", "include", "fc_contracts.h")
    fight_caves_header = os.path.join(repo, "fc-training", "fight_caves.h")

    names = [
        "FC_POLICY_OBS_SIZE",
        "FC_MOVE_DIM",
        "FC_ATTACK_DIM",
        "FC_PRAYER_DIM",
        "FC_EAT_DIM",
        "FC_DRINK_DIM",
    ]
    training_dims = parse_header_constants(shared_header, names)

    puffer_dims = parse_header_constants(
        fight_caves_header, ["FC_PUFFER_OBS_SIZE"], base_values=training_dims
    )
    act_dims = [
        training_dims["FC_MOVE_DIM"],
        training_dims["FC_ATTACK_DIM"],
        training_dims["FC_PRAYER_DIM"],
        training_dims["FC_EAT_DIM"],
        training_dims["FC_DRINK_DIM"],
    ]
    mask_5head_size = sum(act_dims)
    total_line_floats = training_dims["FC_POLICY_OBS_SIZE"] + mask_5head_size

    if total_line_floats != puffer_dims["FC_PUFFER_OBS_SIZE"]:
        raise RuntimeError(
            f"Puffer obs mismatch: viewer line has {total_line_floats} floats, "
            f"training header expects {puffer_dims['FC_PUFFER_OBS_SIZE']}"
        )

    return training_dims["FC_POLICY_OBS_SIZE"], act_dims, mask_5head_size, total_line_floats


def find_viewer():
    """Find the fc_viewer binary."""
    repo = repo_root()
    candidates = [
        "build/fc-viewer/fc_viewer",
        "../build/fc-viewer/fc_viewer",
        "fc-viewer/build/fc_viewer",
    ]
    for p in candidates:
        if os.path.isfile(p):
            return p
    # Try from repo root
    for p in candidates:
        fp = os.path.join(repo, p)
        if os.path.isfile(fp):
            return fp
    return None


def find_latest_checkpoint(env_name="fight_caves"):
    """Find the most recent .bin checkpoint."""
    default_puffer_dir = os.path.join(workspace_root(), "pufferlib_4")
    puffer_dir = os.environ.get("PUFFERLIB_DIR", default_puffer_dir)
    pattern = os.path.join(puffer_dir, "checkpoints", env_name, "**", "*.bin")
    candidates = glob.glob(pattern, recursive=True)
    if not candidates:
        return None
    return max(candidates, key=os.path.getctime)


def read_obs_line(proc, total_line_floats):
    """Read one line of space-separated floats from viewer stdout."""
    line = proc.stdout.readline()
    if not line:
        return None
    values = line.strip().split()
    if len(values) != total_line_floats:
        print(f"[eval] Warning: expected {total_line_floats} floats, got {len(values)}",
              file=sys.stderr)
        return None
    return np.array([float(v) for v in values], dtype=np.float32)


def send_actions(proc, actions):
    """Write 5 space-separated ints to viewer stdin."""
    line = " ".join(str(int(a)) for a in actions) + "\n"
    proc.stdin.write(line)
    proc.stdin.flush()


def sample_masked(logits_list, mask, act_dims, deterministic=False):
    """Sample actions from logits with mask applied."""
    actions = []
    mask_offset = 0
    for head_idx, (logits, dim) in enumerate(zip(logits_list, act_dims)):
        head_mask = mask[mask_offset:mask_offset + dim]
        mask_offset += dim

        # Apply mask: set invalid actions to -inf
        masked_logits = logits.copy()
        for i in range(dim):
            if head_mask[i] < 0.5:
                masked_logits[i] = -1e9

        if deterministic:
            action = np.argmax(masked_logits)
        else:
            # Softmax + sample
            logits_shifted = masked_logits - np.max(masked_logits)
            probs = np.exp(logits_shifted)
            probs = probs / (probs.sum() + 1e-8)
            action = np.random.choice(dim, p=probs)

        actions.append(action)
    return actions


def main():
    # Parse our args FIRST, then clear sys.argv so PufferLib's
    # load_config() doesn't choke on our flags.
    parser = argparse.ArgumentParser(description="Watch trained policy in debug viewer")
    parser.add_argument("--ckpt", type=str, default="latest",
                        help="Path to .bin checkpoint or 'latest'")
    parser.add_argument("--deterministic", action="store_true",
                        help="Use argmax instead of sampling")
    parser.add_argument("--random", action="store_true",
                        help="Use random valid actions (no checkpoint needed)")
    parser.add_argument("--start-wave", type=int, default=0,
                        help="Start at this wave (0 = wave 1)")
    parser.add_argument("--speed", type=int, choices=[1, 2, 4, 10], default=1,
                        help="Initial replay speed multiplier (buttons can switch to TPS presets)")
    parser.add_argument("--episodes", type=int, default=0,
                        help="Stop after this many replay episodes (0 = unlimited)")
    args = parser.parse_args()
    # Clear sys.argv so PufferLib doesn't see our flags
    sys.argv = [sys.argv[0]]
    ensure_local_pufferlib_on_path()

    policy_obs_size, act_dims, mask_5head_size, total_line_floats = load_contract_dims()

    # Find viewer binary
    viewer_path = find_viewer()
    if not viewer_path:
        print("Error: fc_viewer binary not found. Build with: cmake --build build",
              file=sys.stderr)
        sys.exit(1)
    print(f"[eval] Viewer: {viewer_path}", file=sys.stderr)
    print(f"[eval] Replay speed: {args.speed}x", file=sys.stderr)
    if args.episodes > 0:
        print(f"[eval] Episode limit: {args.episodes}", file=sys.stderr)
    print(
        f"[eval] Contract: policy_obs={policy_obs_size} mask5={mask_5head_size} total={total_line_floats}",
        file=sys.stderr,
    )

    # Load checkpoint (unless --random)
    policy = None
    if not args.random:
        checkpoint_path = args.ckpt
        if checkpoint_path == "latest":
            checkpoint_path = find_latest_checkpoint()
            if not checkpoint_path:
                print("Error: no checkpoints found", file=sys.stderr)
                sys.exit(1)
        print(f"[eval] Checkpoint: {checkpoint_path}", file=sys.stderr)

        try:
            import torch
            import pufferlib.models
            from pufferlib.pufferl import load_config

            eval_args = load_config("fight_caves")
            policy_kwargs = eval_args["policy"]
            network_cls = getattr(pufferlib.models, eval_args["torch"]["network"])
            encoder_cls = getattr(pufferlib.models, eval_args["torch"]["encoder"])
            decoder_cls = getattr(pufferlib.models, eval_args["torch"]["decoder"])

            network = network_cls(**policy_kwargs)
            encoder = encoder_cls(total_line_floats, policy_kwargs["hidden_size"])
            decoder = decoder_cls(act_dims, policy_kwargs["hidden_size"])
            policy = pufferlib.models.Policy(encoder, decoder, network)
            policy = policy.cpu()

            # Load raw .bin weights into the PyTorch model.
            # The CUDA trainer saves a flat float32 buffer in this order:
            #   encoder.weight → fused decoder.weight(+value row) → network.layers.0..N
            # PyTorch splits the decoder into separate action/value heads and includes biases.
            weights = np.fromfile(checkpoint_path, dtype=np.float32)
            print(f"[eval] Checkpoint: {len(weights)} floats", file=sys.stderr)

            sd = policy.state_dict()
            # Zero all biases first
            for key in sd:
                if 'bias' in key:
                    sd[key] = torch.zeros_like(sd[key])

            offset = 0

            def load_tensor(key):
                nonlocal offset
                if key not in sd:
                    raise KeyError(f"{key} not in model state_dict")
                numel = sd[key].numel()
                if offset + numel > len(weights):
                    raise RuntimeError(f"weights exhausted at {key}")
                sd[key] = torch.from_numpy(
                    weights[offset:offset+numel].reshape(sd[key].shape).copy())
                offset += numel
                print(f"  loaded {key}: {list(sd[key].shape)} ({numel})", file=sys.stderr)

            load_tensor("encoder.encoder.weight")

            decoder_key = "decoder.decoder.weight"
            value_key = "decoder.value_function.weight"
            if decoder_key not in sd or value_key not in sd:
                raise KeyError("decoder weights missing from model state_dict")

            decoder_rows = sd[decoder_key].shape[0]
            hidden_size = sd[decoder_key].shape[1]
            value_rows = sd[value_key].shape[0]
            fused_rows = decoder_rows + value_rows
            fused_numel = fused_rows * hidden_size
            if offset + fused_numel > len(weights):
                raise RuntimeError("weights exhausted at fused decoder")

            fused_decoder = weights[offset:offset+fused_numel].reshape(fused_rows, hidden_size).copy()
            sd[decoder_key] = torch.from_numpy(fused_decoder[:decoder_rows])
            sd[value_key] = torch.from_numpy(fused_decoder[decoder_rows:])
            offset += fused_numel
            print(
                f"  loaded fused decoder: {list(fused_decoder.shape)} "
                f"-> {list(sd[decoder_key].shape)} + {list(sd[value_key].shape)}",
                file=sys.stderr,
            )

            network_keys = sorted(
                [k for k in sd if k.startswith("network.layers.") and k.endswith(".weight")],
                key=lambda k: int(k.split(".")[2]),
            )
            for key in network_keys:
                load_tensor(key)

            policy.load_state_dict(sd)
            if offset != len(weights):
                print(
                    f"[eval] Warning: loaded {offset}/{len(weights)} floats; "
                    f"{len(weights) - offset} unused",
                    file=sys.stderr,
                )
            else:
                print(f"[eval] Loaded {offset}/{len(weights)} weights", file=sys.stderr)

            policy = policy.cpu()
            policy.eval()
            print("[eval] Policy ready (CPU)", file=sys.stderr)

        except Exception as e:
            print(f"[eval] Cannot load policy: {e}", file=sys.stderr)
            print("[eval] Checkpoint loading failed before replay started.", file=sys.stderr)
            print("[eval] Falling back to random actions", file=sys.stderr)
            args.random = True

    # Launch viewer subprocess from repo root so sprite paths resolve
    print("[eval] Launching viewer...", file=sys.stderr)
    proc = subprocess.Popen(
        [viewer_path, "--policy-pipe", "--speed", str(args.speed)] +
            (["--episodes", str(args.episodes)] if args.episodes > 0 else []) +
            (["--start-wave", str(args.start_wave)] if args.start_wave > 0 else []),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=None,  # let viewer stderr pass through to terminal
        text=True,
        bufsize=1,
        cwd=repo_root(),
    )

    # Hidden state for recurrent policy (MinGRU)
    hidden = None
    if policy is not None:
        import torch
        hidden = policy.initial_state(1, 'cpu')

    try:
        tick = 0
        while True:
            # Read observation from viewer
            obs_data = read_obs_line(proc, total_line_floats)
            if obs_data is None:
                print("[eval] Viewer closed or read error", file=sys.stderr)
                break

            obs = obs_data[:policy_obs_size]
            mask = obs_data[policy_obs_size:]

            if args.random or policy is None:
                # Random valid actions
                actions = sample_masked(
                    [np.zeros(d) for d in act_dims], mask, act_dims, deterministic=False)
            else:
                # Policy inference — feed full puffer observation (policy obs + 5-head mask)
                import torch
                with torch.no_grad():
                    full_input = torch.from_numpy(obs_data).unsqueeze(0)
                    output = policy.forward_eval(full_input, hidden)
                    # forward_eval returns (logits, values, state)
                    logits_raw, _values, hidden = output

                    # Extract per-head logits
                    if isinstance(logits_raw, (list, tuple)):
                        logits_list = [l.squeeze(0).numpy() for l in logits_raw]
                    else:
                        # Single tensor — split by action dims
                        lr = logits_raw.squeeze(0).numpy()
                        logits_list = []
                        off = 0
                        for d in act_dims:
                            logits_list.append(lr[off:off+d])
                            off += d

                actions = sample_masked(logits_list, mask, act_dims, args.deterministic)

            # Send actions to viewer
            send_actions(proc, actions)
            tick += 1

            if tick % 100 == 0:
                print(f"[eval] Tick {tick}", file=sys.stderr)

    except (BrokenPipeError, KeyboardInterrupt):
        print("[eval] Stopped", file=sys.stderr)
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait()


if __name__ == "__main__":
    main()
