import mido
import pytest
from expressive_midi.core import TempoMap, notes_from_midi, render_baseline, flatten_tempo
from expressive_midi.cli import demo


def sample():
    f = mido.MidiFile(ticks_per_beat=480)
    meta = mido.MidiTrack([
        mido.MetaMessage('set_tempo', tempo=500000, time=0),
        mido.MetaMessage('time_signature', numerator=4, denominator=4, time=0),
        mido.MetaMessage('set_tempo', tempo=420000, time=960),
        mido.MetaMessage('end_of_track', time=0)])
    f.tracks.append(meta)
    melody = mido.MidiTrack([
        mido.Message('program_change', program=0, channel=2, time=0),
        mido.Message('note_on', note=60, velocity=90, channel=2, time=0),
        mido.Message('note_on', note=64, velocity=91, channel=2, time=0),
        mido.Message('note_off', note=60, velocity=0, channel=2, time=360),
        mido.Message('note_on', note=64, velocity=0, channel=2, time=0),
        mido.Message('control_change', control=64, value=127, channel=2, time=120),
        mido.Message('note_on', note=60, velocity=80, channel=2, time=0),
        mido.Message('note_off', note=60, velocity=0, channel=2, time=480),
        mido.Message('control_change', control=64, value=0, channel=2, time=0),
        mido.MetaMessage('end_of_track', time=0)])
    f.tracks.append(melody)
    return f


def absolute_by_type(midi, typ):
    res=[]
    for t, track in enumerate(midi.tracks):
        tick=0
        for msg in track:
            tick+=msg.time
            if msg.type == typ:
                res.append((t,tick,{k:v for k,v in msg.dict().items() if k != 'time'}))
    return res


def test_tempo_map_invert():
    tm=TempoMap(sample())
    assert tm.to_seconds(480) == pytest.approx(.5)
    assert tm.to_seconds(1440) == pytest.approx(1.42)
    for tick in (0, 230, 480, 960, 1400):
        assert abs(tm.to_ticks(tm.to_seconds(tick)) - tick) <= 1


def test_identity_exact():
    f=sample()
    o=render_baseline(f,strength=0)
    assert [[m.dict() for m in track] for track in f.tracks] == [[m.dict() for m in track] for track in o.tracks]


def test_preserve_pitches_non_notes_and_channels():
    f=sample()
    o=render_baseline(f, seed=44)
    assert sorted((n.pitch,n.channel,n.track) for n in notes_from_midi(f)) == sorted((n.pitch,n.channel,n.track) for n in notes_from_midi(o))
    for typ in ('set_tempo','time_signature','control_change','program_change'):
        assert absolute_by_type(f,typ)==absolute_by_type(o,typ)
    assert len(notes_from_midi(o))==3
    assert mido.MidiFile(file=__import__('io').BytesIO(_save(o)))


def _save(midi):
    from io import BytesIO
    b=BytesIO();midi.save(file=b);return b.getvalue()


def test_reproducible():
    a=render_baseline(sample(),seed=42)
    b=render_baseline(sample(),seed=42)
    c=render_baseline(sample(),seed=43)
    assert _save(a)==_save(b)
    assert _save(a)!=_save(c)


def test_note_lengths():
    o=render_baseline(sample(),strength=1.0,seed=17)
    assert all(n.offset_tick > n.onset_tick for n in notes_from_midi(o))


def test_strength_validation():
    with pytest.raises(ValueError): render_baseline(sample(),strength=3)


def test_demo(tmp_path):
    path=tmp_path/'demo.mid'
    f=demo(path)
    assert len(notes_from_midi(mido.MidiFile(path))) > 50
    assert len(f.tracks)==2


def test_flatten_tempo_seconds_match():
    source = sample()
    dst = flatten_tempo(source, bpm=120.0)
    tempo = TempoMap(dst)
    assert all(msg.tempo == 500000 for track in dst.tracks for msg in track if msg.type == 'set_tempo')
    before = sorted((n.pitch, TempoMap(source).to_seconds(n.onset_tick),
                     TempoMap(source).to_seconds(n.offset_tick)) for n in notes_from_midi(source))
    after = sorted((n.pitch, tempo.to_seconds(n.onset_tick),
                    tempo.to_seconds(n.offset_tick)) for n in notes_from_midi(dst))
    for (pitch, t1, t2), (npitch, n1, n2) in zip(before, after):
        assert pitch == npitch
        assert t1 == pytest.approx(n1, abs=.002)
        assert t2 == pytest.approx(n2, abs=.002)


def test_invalid_flatten_bpm():
    with pytest.raises(ValueError):
        flatten_tempo(sample(), bpm=0)
