"""Entry point used by the VST3 plugin's AI engine.

The plugin runs this module with the private Python environment it installed:

    python -m expressive_midi.ai_bridge render --score S.mid --raw R.mid --model DIR
    python -m expressive_midi.ai_bridge blend --score S.mid --raw R.mid --out O.mid --bpm 120 --strength 0.8
    python -m expressive_midi.ai_bridge check --model DIR
    python -m expressive_midi.ai_bridge download-model --dest DIR

``render`` runs Pianist Transformer once (slow). ``blend`` mixes that raw performance
with the score at a given strength and DAW tempo (fast), so moving the plugin's
strength slider does not run the model again.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

VENDOR = Path(__file__).resolve().parent / '_vendor' / 'pianist_transformer'


def resolve_device(name: str, torch) -> str:
    if name == 'auto':
        if torch.cuda.is_available():
            return 'cuda'
        mps = getattr(getattr(torch, 'backends', None), 'mps', None)
        if mps is not None and mps.is_available():
            return 'mps'
        return 'cpu'
    if name == 'cuda' and not torch.cuda.is_available():
        raise RuntimeError('CUDA requested but not available')
    if name == 'mps' and not torch.backends.mps.is_available():
        raise RuntimeError('MPS requested but not available')
    return name


def render(score: Path, raw: Path, model_dir: Path, device: str = 'auto',
           temperature: float = 1.0, top_p: float = 0.95) -> str:
    if str(VENDOR) not in sys.path:
        sys.path.insert(0, str(VENDOR))
    import torch
    from miditoolkit import MidiFile
    from src.model.generate import batch_performance_render, map_midi
    from src.model.pianoformer import PianoT5Gemma

    device = resolve_device(device, torch)
    print(f'STAGE load-model {device}', flush=True)
    dtype = torch.float32 if device == 'cpu' else torch.bfloat16
    model = PianoT5Gemma.from_pretrained(str(model_dir), torch_dtype=dtype).to(device).eval()
    print('STAGE render', flush=True)
    midi = MidiFile(str(score))
    with torch.inference_mode():
        rendered, _ = batch_performance_render(model, [midi], temperature=temperature,
                                               top_p=top_p, device=device)
    if not rendered:
        raise RuntimeError('Pianist Transformer returned no performance')
    raw.parent.mkdir(parents=True, exist_ok=True)
    map_midi(midi, rendered[0]).dump(str(raw))
    return device


def blend(score: Path, raw: Path, out: Path, bpm: float, strength: float, pedal: str = 'model') -> dict:
    import mido
    from .postprocess import blend_performance
    result, stats = blend_performance(mido.MidiFile(score), mido.MidiFile(raw),
                                      bpm=bpm, strength=strength, pedal=pedal)
    out.parent.mkdir(parents=True, exist_ok=True)
    result.save(out)
    return {'notes': stats.notes, 'velocity_changed': stats.velocity_changed,
            'onset_changed': stats.onset_changed, 'pedal_events': stats.pedal_events}


MODEL_REPO = 'yhj137/pianist-transformer-rendering'


def download_model(dest: Path, repo_id: str = MODEL_REPO) -> Path:
    """Download the fine-tuned rendering checkpoint (Apache-2.0) from Hugging Face."""
    from huggingface_hub import snapshot_download
    dest.mkdir(parents=True, exist_ok=True)
    snapshot_download(repo_id=repo_id, local_dir=str(dest))
    missing = [n for n in ('config.json', 'model.safetensors') if not (dest / n).is_file()]
    if missing:
        raise RuntimeError(f'download incomplete, missing: {", ".join(missing)}')
    return dest


def check(model_dir: Path) -> dict:
    info = {name: (model_dir / name).is_file()
            for name in ('config.json', 'generation_config.json', 'model.safetensors')}
    try:
        import torch, transformers, miditoolkit  # noqa: F401
        info['imports'] = True
    except ImportError as exc:  # pragma: no cover - depends on environment
        info['imports'] = False
        info['import_error'] = str(exc)
    return info


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('render')
    r.add_argument('--score', type=Path, required=True)
    r.add_argument('--raw', type=Path, required=True)
    r.add_argument('--model', type=Path, required=True)
    r.add_argument('--device', default='auto', choices=['auto', 'cpu', 'cuda', 'mps'])
    r.add_argument('--temperature', type=float, default=1.0)
    r.add_argument('--top-p', type=float, default=0.95)
    b = sub.add_parser('blend')
    b.add_argument('--score', type=Path, required=True)
    b.add_argument('--raw', type=Path, required=True)
    b.add_argument('--out', type=Path, required=True)
    b.add_argument('--bpm', type=float, required=True)
    b.add_argument('--strength', type=float, default=1.0)
    b.add_argument('--pedal', default='model', choices=['model', 'score', 'none'])
    c = sub.add_parser('check')
    c.add_argument('--model', type=Path, required=True)
    d = sub.add_parser('download-model')
    d.add_argument('--dest', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.cmd == 'render':
            device = render(args.score, args.raw, args.model, args.device, args.temperature, args.top_p)
            print(json.dumps({'device': device}), flush=True)
        elif args.cmd == 'download-model':
            print(json.dumps({'model': str(download_model(args.dest))}), flush=True)
        elif args.cmd == 'blend':
            print(json.dumps(blend(args.score, args.raw, args.out, args.bpm, args.strength, args.pedal)), flush=True)
        else:
            info = check(args.model)
            print(json.dumps(info), flush=True)
            return 0 if all(v for k, v in info.items() if k != 'import_error') else 1
        return 0
    except Exception as exc:  # report a single readable line to the plugin
        print(f'ERROR {type(exc).__name__}: {exc}', file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
