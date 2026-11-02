from io import BytesIO
import mido
import pytest

from expressive_midi.core import TempoMap, notes_from_midi
from expressive_midi.postprocess import blend_performance
from expressive_midi.cli import main


def midi(pitches=(60, 64), velocities=(80, 70), starts=(0, 0), lengths=(480, 480),
         *, tpb=480, tempo=500000, pedal=None):
    out = mido.MidiFile(ticks_per_beat=tpb)
    out.tracks.append(mido.MidiTrack([mido.MetaMessage('set_tempo', tempo=tempo),
                                      mido.MetaMessage('time_signature', numerator=4, denominator=4)]))
    events = [(0, 1, mido.Message('program_change', channel=2, program=0))]
    for pitch, vel, st, length in zip(pitches, velocities, starts, lengths):
        events.extend([(st, 3, mido.Message('note_on', channel=2, note=pitch, velocity=vel)),
                       (st+length, 2, mido.Message('note_off', channel=2, note=pitch))])
    for tick, value in pedal or []:
        events.append((tick, 0, mido.Message('control_change', channel=2, control=64, value=value)))
    tr = mido.MidiTrack()
    last = 0
    for tick, _, msg in sorted(events, key=lambda event: (event[0],event[1])):
        tr.append(msg.copy(time=tick-last))
        last = tick
    out.tracks.append(tr)
    return out


def get_cc(mf, number=64):
    return [msg for tr in mf.tracks for msg in tr if msg.type == 'control_change' and msg.control == number]


def test_zero_strength_preserves_score_and_tempo():
    score = midi(pedal=[(200, 127), (900, 0)])
    perf = midi(velocities=(30, 120), starts=(35, 42), lengths=(360, 700), pedal=[(140, 127)])
    out, stat = blend_performance(score, perf, bpm=120, strength=0)
    assert [(n.pitch,n.velocity,n.onset_tick,n.offset_tick,n.channel) for n in notes_from_midi(out)] == [
        (n.pitch,n.velocity,n.onset_tick,n.offset_tick,n.channel) for n in notes_from_midi(score)]
    assert [m.value for m in get_cc(out)] == [127, 0]
    assert stat.notes == 2


def test_full_strength_matches_performance_different_resolution():
    score = midi()
    perf = midi(velocities=(41, 112), starts=(25, 30), lengths=(340, 550), tpb=960,
                tempo=500000, pedal=[(10,127),(800,0)])
    out, stat = blend_performance(score, perf, bpm=120, strength=1)
    got = sorted(notes_from_midi(out), key=lambda n:n.pitch)
    assert [n.pitch for n in got] == [60,64]
    assert [n.velocity for n in got] == [41,112]
    assert [n.onset_tick for n in got] == [12,15]
    assert [n.offset_tick for n in got] == [182,290]
    assert [n.channel for n in got] == [2,2]
    assert len(get_cc(out)) == 2
    assert get_cc(out)[0].channel == 2
    assert stat.pedal_events == 2


def test_half_strength_interpolates_velocity_and_timing():
    score = midi(pitches=(60,), velocities=(80,), starts=(100,), lengths=(300,))
    perf = midi(pitches=(60,), velocities=(40,), starts=(200,), lengths=(500,))
    out, stat = blend_performance(score, perf, bpm=120, strength=.5)
    n = notes_from_midi(out)[0]
    assert n.velocity == 60
    assert n.onset_tick == 150
    assert n.offset_tick == 550
    assert stat.velocity_changed == stat.onset_changed == stat.duration_changed == 1


def test_no_pedal_mode():
    score = midi(pedal=[(100,127)])
    perf = midi(pedal=[(200,127)])
    out, stats = blend_performance(score, perf, pedal='none')
    assert get_cc(out) == []
    assert stats.pedal_events == 0


def test_score_pedal_mode():
    score = midi(pedal=[(100,127)])
    perf = midi(pedal=[(200,0)])
    out, _ = blend_performance(score, perf, pedal='score')
    assert [m.value for m in get_cc(out)] == [127]


def test_note_pitch_mismatch_fails():
    with pytest.raises(ValueError,match='pitches/counts'):
        blend_performance(midi(), midi(pitches=(60, 65)))


@pytest.mark.parametrize('strength, bpm, pedal', [(-.1,120,'model'), (1.01,120,'model'),
                                                (.5,0,'model'),(.5,120,'wrong'),
                                                (float('nan'),120,'model')])
def test_invalid_input(strength,bpm,pedal):
    with pytest.raises(ValueError):
        blend_performance(midi(),midi(),strength=strength,bpm=bpm,pedal=pedal)


def test_generated_midi_is_valid_on_disk(tmp_path):
    score = midi()
    score.save(tmp_path/'score.mid')
    code = main(['render',str(tmp_path/'score.mid'),str(tmp_path/'out.mid'),
                 '--backend','baseline','--daw-bpm','120','--report'])
    assert code == 0
    out = mido.MidiFile(tmp_path/'out.mid')
    assert len(notes_from_midi(out)) == 2
    assert len([m for tr in out.tracks for m in tr if m.type == 'set_tempo']) == 1


def test_overlapping_same_pitch_output_has_positive_duration():
    score = midi(pitches=(60,60), starts=(0,480), lengths=(470,490))
    perf = midi(pitches=(60,60), starts=(0,470), lengths=(550,490))
    out, _ = blend_performance(score, perf)
    notes = sorted(notes_from_midi(out),key=lambda n:n.onset_tick)
    assert len(notes)==2
    assert notes[0].offset_tick < notes[1].onset_tick
    assert all(n.offset_tick > n.onset_tick for n in notes)


def test_preserves_different_source_tempo_as_real_time():
    score=midi(pitches=(60,),starts=(480,),lengths=(480,),tempo=400000)
    perf=midi(pitches=(60,),starts=(480,),lengths=(480,),tempo=500000)
    out,_ = blend_performance(score,perf,bpm=120, strength=0)
    assert TempoMap(out).to_seconds(notes_from_midi(out)[0].onset_tick) == pytest.approx(.4,abs=.002)
    out,_ = blend_performance(score,perf,bpm=120,strength=1)
    assert TempoMap(out).to_seconds(notes_from_midi(out)[0].onset_tick) == pytest.approx(.5,abs=.002)
