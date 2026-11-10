"""Tests for the plugin's AI entry point. The model itself is faked; no real inference."""
from contextlib import nullcontext
import json
import sys
import types

import mido

from expressive_midi import ai_bridge
from expressive_midi.core import notes_from_midi
from tests.test_postprocess import midi


def fake_upstream(monkeypatch, seen, cuda=False):
    torch = types.ModuleType('torch')
    torch.float32, torch.bfloat16 = 'float32', 'bfloat16'
    torch.cuda = types.SimpleNamespace(is_available=lambda: cuda)
    torch.backends = types.SimpleNamespace(mps=types.SimpleNamespace(is_available=lambda: False))
    torch.inference_mode = nullcontext
    monkeypatch.setitem(sys.modules, 'torch', torch)
    mtk = types.ModuleType('miditoolkit')

    class MidiFile:
        def __init__(self, filename):
            self.filename = str(filename)

        def dump(self, filename):
            mido.MidiFile(self.filename).save(filename)
    mtk.MidiFile = MidiFile
    monkeypatch.setitem(sys.modules, 'miditoolkit', mtk)

    class Model:
        @classmethod
        def from_pretrained(cls, path, torch_dtype):
            seen['path'], seen['dtype'] = path, torch_dtype
            return cls()

        def to(self, device):
            seen['device'] = device
            return self

        def eval(self):
            return self
    gen = types.ModuleType('src.model.generate')
    gen.batch_performance_render = lambda model, scores, temperature, top_p, device: (scores, None)
    gen.map_midi = lambda score, rendered: rendered
    pf = types.ModuleType('src.model.pianoformer')
    pf.PianoT5Gemma = Model
    for name, module in [('src', types.ModuleType('src')), ('src.model', types.ModuleType('src.model')),
                         ('src.model.generate', gen), ('src.model.pianoformer', pf)]:
        monkeypatch.setitem(sys.modules, name, module)


def test_render_auto_device_uses_cpu_without_gpu(monkeypatch, tmp_path):
    seen = {}
    fake_upstream(monkeypatch, seen)
    score, raw = tmp_path / 's.mid', tmp_path / 'r.mid'
    midi().save(score)
    assert ai_bridge.main(['render', '--score', str(score), '--raw', str(raw), '--model', str(tmp_path)]) == 0
    assert raw.is_file() and seen['device'] == 'cpu' and seen['dtype'] == 'float32'


def test_render_auto_device_prefers_cuda(monkeypatch, tmp_path):
    seen = {}
    fake_upstream(monkeypatch, seen, cuda=True)
    score = tmp_path / 's.mid'
    midi().save(score)
    ai_bridge.render(score, tmp_path / 'r.mid', tmp_path, 'auto')
    assert seen['device'] == 'cuda' and seen['dtype'] == 'bfloat16'


def test_blend_writes_constant_tempo_output(tmp_path, capsys):
    score, raw, out = tmp_path / 's.mid', tmp_path / 'r.mid', tmp_path / 'o.mid'
    midi().save(score)
    midi(velocities=(110, 40), starts=(20, 30)).save(raw)
    assert ai_bridge.main(['blend', '--score', str(score), '--raw', str(raw), '--out', str(out),
                           '--bpm', '100', '--strength', '0.5']) == 0
    stats = json.loads(capsys.readouterr().out.strip().splitlines()[-1])
    assert stats['notes'] == 2
    result = mido.MidiFile(out)
    assert sorted(n.velocity for n in notes_from_midi(result)) == [55, 95]
    tempos = [m.tempo for t in result.tracks for m in t if m.type == 'set_tempo']
    assert tempos == [600000]


def test_errors_are_one_readable_line(tmp_path, capsys):
    assert ai_bridge.main(['blend', '--score', str(tmp_path / 'missing.mid'), '--raw', str(tmp_path / 'x.mid'),
                           '--out', str(tmp_path / 'o.mid'), '--bpm', '120']) == 1
    assert capsys.readouterr().err.startswith('ERROR ')


def test_check_reports_missing_weights(tmp_path, capsys):
    assert ai_bridge.main(['check', '--model', str(tmp_path)]) == 1
    info = json.loads(capsys.readouterr().out)
    assert info['model.safetensors'] is False


def test_download_model_uses_hugging_face_snapshot(monkeypatch, tmp_path):
    hub = types.ModuleType('huggingface_hub')
    calls = {}

    def snapshot_download(repo_id, local_dir):
        calls['repo'] = repo_id
        for name in ('config.json', 'generation_config.json', 'model.safetensors'):
            (tmp_path / 'm' / name).write_text('x')
    hub.snapshot_download = snapshot_download
    monkeypatch.setitem(sys.modules, 'huggingface_hub', hub)
    assert ai_bridge.main(['download-model', '--dest', str(tmp_path / 'm')]) == 0
    assert calls['repo'] == 'yhj137/pianist-transformer-rendering'
