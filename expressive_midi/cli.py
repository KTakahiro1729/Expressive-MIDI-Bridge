"""CLI for deterministic baseline and external PianistTransformer inference."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import mido

from .core import render_baseline, notes_from_midi, flatten_tempo
from .postprocess import blend_performance


def demo(path: Path):
    mf = mido.MidiFile(ticks_per_beat=480)
    meta = mido.MidiTrack([mido.MetaMessage('track_name', name='Original demo'),
                           mido.MetaMessage('set_tempo', tempo=500000),
                           mido.MetaMessage('time_signature', numerator=4, denominator=4),
                           mido.MetaMessage('end_of_track')])
    mf.tracks.append(meta)
    tr = mido.MidiTrack()
    # Original eight-bar piano exercise, C/Am/F/G changes, melody & left hand.
    chords = [(48,52,55),(45,48,52),(41,45,48),(43,47,50)] * 2
    events = []
    for bar, chord in enumerate(chords):
        for n in chord:
            events += [(bar*1920, 1, mido.Message('note_on', note=n, velocity=62, channel=0)),
                       ((bar+1)*1920-80, 0, mido.Message('note_off', note=n, velocity=0, channel=0))]
        for step in range(8):
            note = 60 + [0,2,4,7,9,7,4,2][step] + (0 if bar % 2 == 0 else -2)
            start = bar*1920 + step*240
            events += [(start, 1, mido.Message('note_on', note=note, velocity=78, channel=0)),
                       (start+190, 0, mido.Message('note_off', note=note, velocity=0, channel=0))]
    prev = 0
    for tick, _, msg in sorted(events, key=lambda r:(r[0],r[1])):
        tr.append(msg.copy(time=tick-prev)); prev=tick
    tr.append(mido.MetaMessage('end_of_track'))
    mf.tracks.append(tr)
    path.parent.mkdir(parents=True, exist_ok=True)
    mf.save(path)
    return mf


def ai_render(input_file: Path, output_file: Path, args):
    repo = Path(args.repo).expanduser().resolve() if args.repo else None
    if not repo or not (repo / 'src' / 'model' / 'generate.py').is_file():
        raise RuntimeError('PianistTransformer repository not found; provide --repo PATH to cloned repo')
    checkpoint = Path(args.checkpoint).expanduser().resolve() if args.checkpoint else repo / 'models' / 'sft'
    if not (checkpoint / 'config.json').is_file():
        raise RuntimeError('Model weights not found; run the upstream download_model command before rendering')
    runner = Path(__file__).resolve().parent / 'integrations' / 'runner.py'
    cmd = [args.model_python or sys.executable, str(runner), '--repo', str(repo),
           '--checkpoint', str(checkpoint), '--input', str(input_file),
           '--output', str(output_file), '--device', args.device,
           '--temperature', str(args.temperature), '--top-p', str(args.top_p)]
    env = os.environ.copy()
    env['PYTHONPATH'] = str(repo) + os.pathsep + env.get('PYTHONPATH', '')
    subprocess.run(cmd, env=env, cwd=str(repo), check=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Generate expressive performance MIDI for any MIDI piano instrument.')
    sub = parser.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('demo', help='Create an original score MIDI for testing')
    d.add_argument('-o', '--output', type=Path, default=Path('demo/score.mid'))
    p = sub.add_parser('render', help='Transform score MIDI into performance MIDI')
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--backend', choices=['baseline','identity','pianist-transformer'], default='baseline')
    p.add_argument('--strength', type=float, default=.65)
    p.add_argument('--style', choices=['classical','pop','jazz'], default='classical')
    p.add_argument('--seed', type=int, default=23)
    p.add_argument('--repo', help='PianistTransformer repo (required for AI backend)')
    p.add_argument('--checkpoint', help='Checkpoint directory (default: repo/models/sft)')
    p.add_argument('--model-python', help='Python executable in PianistTransformer environment')
    p.add_argument('--device', default='cpu', choices=['cpu','cuda','mps'])
    p.add_argument('--temperature', type=float, default=1.0)
    p.add_argument('--top-p', type=float, default=.95)
    p.add_argument('--report', action='store_true')
    p.add_argument('--daw-bpm', type=float, help='Convert output to constant DAW tempo in BPM')
    p.add_argument('--ai-strength', type=float, default=1.0, help='Mix ratio for AI model timing/velocity, 0..1')
    p.add_argument('--pedal', choices=['model','score','none'], default='model', help='How to handle sustain CC64 in AI blend')
    f = sub.add_parser('flatten-tempo', help='Flatten a variable-tempo performance MIDI to a constant DAW BPM')
    f.add_argument('input', type=Path)
    f.add_argument('output', type=Path)
    f.add_argument('--bpm', type=float, default=120)
    check = sub.add_parser('doctor', help='Check AI repo, model weights, and Python environment')
    check.add_argument('--repo', type=Path, required=True)
    check.add_argument('--checkpoint', type=Path)
    check.add_argument('--model-python', default=sys.executable)
    args = parser.parse_args(argv)
    try:
        if args.cmd == 'doctor':
            repo = args.repo.expanduser().resolve()
            checkpoint = (args.checkpoint.expanduser().resolve() if args.checkpoint
                          else repo/'models'/'sft')
            info = {
                'repo_exists': (repo/'src'/'model'/'generate.py').is_file(),
                'model_config': (checkpoint/'config.json').is_file(),
                'model_weights': (checkpoint/'model.safetensors').is_file(),
                'model_generation_config': (checkpoint/'generation_config.json').is_file(),
                'python': args.model_python,
            }
            if info['repo_exists']:
                cmd = [args.model_python, '-c',
                       'import torch, transformers, miditoolkit; from src.model.pianoformer import PianoT5Gemma; print("imports OK")']
                try:
                    probe = subprocess.run(cmd, cwd=repo, capture_output=True, text=True,
                                           timeout=120, check=False)
                    info['imports_ok'] = (probe.returncode == 0)
                    if probe.returncode != 0:
                        info['import_error'] = probe.stderr[-1200:]
                except (OSError, subprocess.TimeoutExpired) as exc:
                    info['imports_ok'] = False
                    info['import_error'] = str(exc)
            else:
                info['imports_ok'] = False
            print(json.dumps(info, indent=2, ensure_ascii=False))
            return 0 if all(info[k] for k in ('repo_exists','model_config','model_weights',
                                              'model_generation_config','imports_ok')) else 1
        if args.cmd == 'demo':
            demo(args.output)
            print(f'Created {args.output}')
            return 0
        if args.cmd == 'flatten-tempo':
            if not args.input.is_file():
                parser.error(f'Input MIDI not found: {args.input}')
            args.output.parent.mkdir(parents=True, exist_ok=True)
            flatten_tempo(mido.MidiFile(args.input), bpm=args.bpm).save(args.output)
            print(f'Created {args.output} (constant BPM {args.bpm})')
            return 0
        if not args.input.is_file():
            parser.error(f'Input MIDI not found: {args.input}')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        original = mido.MidiFile(args.input)
        if original.type == 2:
            parser.error('MIDI type 2 is unsupported')
        if args.backend == 'pianist-transformer':
            ai_render(args.input.resolve(), args.output.resolve(), args)
            if args.daw_bpm is not None:
                model_midi = mido.MidiFile(args.output)
                blended, stats = blend_performance(original, model_midi, bpm=args.daw_bpm,
                                                  strength=args.ai_strength, pedal=args.pedal)
                blended.save(args.output)
                print(f'AI blend: {stats.notes} notes; velocities={stats.velocity_changed}, '
                      f'onsets={stats.onset_changed}, pedals={stats.pedal_events}')
        else:
            if args.backend == 'identity':
                result = render_baseline(original, strength=0)
            else:
                result = render_baseline(original, strength=args.strength, seed=args.seed, style=args.style)
            if args.daw_bpm is not None:
                result = flatten_tempo(result, bpm=args.daw_bpm)
            result.save(args.output)
        if args.report:
            output = mido.MidiFile(args.output)
            print(json.dumps({'backend': args.backend, 'input_notes': len(notes_from_midi(original)),
                              'output_notes': len(notes_from_midi(output)),
                              'tracks': len(output.tracks), 'input':str(args.input),
                              'output':str(args.output)}, ensure_ascii=False, indent=2))
        else:
            print(f'Created {args.output} (backend={args.backend})')
        return 0
    except (RuntimeError, ValueError, subprocess.CalledProcessError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
