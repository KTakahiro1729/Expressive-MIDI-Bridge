"""Adapter to the upstream PianistTransformer repository's public Python API.

Upstream library/weights are NOT copied into this project. This adapter must be
run under the Python 3.11 environment with the upstream dependencies installed.
Not executed in the build environment; API based on upstream Nov 2026 main.
"""
import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device', default='cpu')
    parser.add_argument('--temperature', type=float, default=1.0)
    parser.add_argument('--top-p', type=float, default=.95)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo.resolve()))
    import torch
    from miditoolkit import MidiFile
    from src.model.generate import batch_performance_render, map_midi
    from src.model.pianoformer import PianoT5Gemma

    device = args.device
    if device == 'auto':
        mps = getattr(torch.backends, 'mps', None)
        device = 'cuda' if torch.cuda.is_available() else (
            'mps' if mps is not None and mps.is_available() else 'cpu')
    if device == 'cuda' and not torch.cuda.is_available():
        raise RuntimeError('CUDA requested but not available')
    if device == 'mps' and not torch.backends.mps.is_available():
        raise RuntimeError('MPS requested but not available')
    dtype = torch.float32 if device == 'cpu' else torch.bfloat16
    model = PianoT5Gemma.from_pretrained(str(args.checkpoint), torch_dtype=dtype)
    model = model.to(device).eval()
    score = MidiFile(str(args.input))
    with torch.inference_mode():
        rendered, _ = batch_performance_render(model, [score], temperature=args.temperature,
                                               top_p=args.top_p, device=device)
    if not rendered:
        raise RuntimeError('PianistTransformer returned no performances')
    mapped = map_midi(score, rendered[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    mapped.dump(str(args.output))


if __name__ == '__main__':
    main()
