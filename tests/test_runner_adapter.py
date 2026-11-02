"""Contract test for the adapter; not a claim of real model inference."""
from contextlib import nullcontext
import sys
import types

from expressive_midi.integrations import runner


def test_adapter_calls_upstream_api(monkeypatch, tmp_path):
    fake_torch = types.ModuleType('torch')
    fake_torch.float32 = 'float32'
    fake_torch.bfloat16 = 'bfloat16'
    fake_torch.cuda = types.SimpleNamespace(is_available=lambda: False)
    fake_torch.backends = types.SimpleNamespace(mps=types.SimpleNamespace(is_available=lambda: False))
    fake_torch.inference_mode = nullcontext
    monkeypatch.setitem(sys.modules, 'torch', fake_torch)
    fake_miditoolkit = types.ModuleType('miditoolkit')
    class MidiFile:
        def __init__(self, filename):
            self.filename = str(filename)
        def dump(self, filename):
            from pathlib import Path
            Path(filename).write_bytes(b'FAKE MIDI CONTENT')
    fake_miditoolkit.MidiFile = MidiFile
    monkeypatch.setitem(sys.modules, 'miditoolkit', fake_miditoolkit)
    root = types.ModuleType('src')
    model = types.ModuleType('src.model')
    generate = types.ModuleType('src.model.generate')
    pianoformer = types.ModuleType('src.model.pianoformer')
    seen = {}
    class FakeModel:
        @classmethod
        def from_pretrained(cls, path, torch_dtype):
            seen['checkpoint'] = path
            seen['dtype'] = torch_dtype
            return cls()
        def to(self, device):
            seen['device'] = device
            return self
        def eval(self):
            return self
    def fake_render(model, scores, temperature, top_p, device):
        seen['temperature'] = temperature
        seen['top_p'] = top_p
        return [MidiFile('rendered')], []
    def fake_map(score, rendered):
        seen['score_file'] = score.filename
        return rendered
    pianoformer.PianoT5Gemma = FakeModel
    generate.batch_performance_render = fake_render
    generate.map_midi = fake_map
    for name, module in [('src',root),('src.model',model),('src.model.generate',generate),
                         ('src.model.pianoformer',pianoformer)]:
        monkeypatch.setitem(sys.modules, name, module)
    inp, out = tmp_path/'a.mid',tmp_path/'b.mid'
    inp.write_bytes(b'dummy')
    monkeypatch.setattr(sys,'argv',['runner','--repo',str(tmp_path),
                                     '--checkpoint',str(tmp_path), '--input',str(inp),
                                     '--output',str(out),'--temperature','0.85','--top-p','0.93'])
    runner.main()
    assert out.read_bytes() == b'FAKE MIDI CONTENT'
    assert seen['dtype'] == 'float32'
    assert seen['temperature'] == 0.85
    assert seen['top_p'] == 0.93
    assert seen['score_file'] == str(inp)
