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
    with pytest.raises(ValueError,match='differ from the score'):
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


def test_unison_doublings_match_one_model_note():
    # Two voices write the same C at the same time; the model returns that C once.
    score = midi(pitches=(60, 60, 64), velocities=(80, 70, 60), starts=(0, 0, 480), lengths=(480, 240, 480))
    perf = midi(pitches=(60, 64), velocities=(100, 40), starts=(30, 500), lengths=(450, 470))
    out, stats = blend_performance(score, perf, bpm=120, strength=1.0)
    notes = sorted((n.pitch, n.velocity) for n in notes_from_midi(out))
    assert notes == [(60, 100), (60, 100), (64, 40)]
    assert stats.notes == 3


def test_other_mismatches_still_raise():
    score = midi(pitches=(60, 60), starts=(0, 480))
    perf = midi(pitches=(60,), starts=(0,), velocities=(80,), lengths=(480,))
    with pytest.raises(ValueError, match='differ from the score'):
        blend_performance(score, perf, bpm=120)


def _bar_test_files(drift_per_bar_ticks=60, micro=(0, 25, -15, 10)):
    """4 bars of quarter notes; the 'performance' slows down a little more every bar
    (cumulative drift) and adds a fixed micro-timing pattern inside each bar."""
    pitches, starts, perf_starts = [], [], []
    for bar in range(4):
        for beat in range(4):
            pitches.append(60 + beat)
            starts.append(bar * 1920 + beat * 480)
            perf_starts.append(bar * 1920 + beat * 480 + bar * drift_per_bar_ticks
                               + beat * drift_per_bar_ticks // 4 + micro[beat])
    n = len(pitches)
    score = midi(pitches=pitches, velocities=[64] * n, starts=starts, lengths=[400] * n)
    perf = midi(pitches=pitches, velocities=[90] * n, starts=perf_starts, lengths=[400] * n,
                pedal=[(1920 + 60, 127), (3840 + 120, 0)])
    return score, perf, starts


def _onsets(mf):
    return sorted((n.onset_tick, n.pitch) for n in notes_from_midi(mf))


def test_align_bars_puts_downbeats_on_the_score_bar_lines():
    score, perf, starts = _bar_test_files()
    free, _ = blend_performance(score, perf, bpm=120, strength=1.0, align='none')
    aligned, _ = blend_performance(score, perf, bpm=120, strength=1.0, align='bars')
    downbeats = [s for s in starts if s % 1920 == 0]
    got_free = [t for t, p in _onsets(free) if p == 60]
    got_aligned = [t for t, p in _onsets(aligned) if p == 60]
    # Without alignment the last downbeat drifts by 3 bars x 60 ticks.
    assert got_free[-1] - downbeats[-1] == 180
    # With alignment every downbeat sits on the score's bar line (within 1 tick).
    assert all(abs(g - d) <= 1 for g, d in zip(got_aligned, downbeats))


def test_align_bars_keeps_expression_inside_the_bar():
    score, perf, starts = _bar_test_files(drift_per_bar_ticks=0)
    aligned, _ = blend_performance(score, perf, bpm=120, strength=1.0, align='bars')
    # No drift: alignment must not remove the micro-timing (+25, -15, +10 ticks).
    got = [t for t, _ in _onsets(aligned)]
    assert got[1] - starts[1] == 25 and got[2] - starts[2] == -15 and got[3] - starts[3] == 10
    # Velocities still come from the model.
    assert {n.velocity for n in notes_from_midi(aligned)} == {90}


def test_align_bars_moves_the_model_pedal_with_the_notes():
    score, perf, _ = _bar_test_files()
    aligned, _ = blend_performance(score, perf, bpm=120, strength=1.0, align='bars', pedal='model')
    ticks, t = [], 0
    for tr in aligned.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            if msg.type == 'control_change' and msg.control == 64:
                ticks.append(t)
    # The pedal was 60 / 120 ticks late together with bars 2 and 3; now it is on the bar line.
    assert [abs(x - y) <= 1 for x, y in zip(ticks, (1920, 3840))] == [True, True]


def test_align_rejects_unknown_mode():
    with pytest.raises(ValueError, match='align'):
        blend_performance(midi(), midi(), bpm=120, align='beats')


def test_bar_lines_follow_time_signature_and_tempo_changes():
    from expressive_midi.postprocess import bar_lines_seconds
    mf = mido.MidiFile(ticks_per_beat=480)
    mf.tracks.append(mido.MidiTrack([
        mido.MetaMessage('time_signature', numerator=3, denominator=4, time=0),
        mido.MetaMessage('set_tempo', tempo=500000, time=0),          # 120 BPM
        mido.MetaMessage('time_signature', numerator=4, denominator=4, time=2 * 1440),
        mido.MetaMessage('set_tempo', tempo=1000000, time=1920),      # 60 BPM from bar 4
    ]))
    lines = bar_lines_seconds(mf, until_sec=6.0)
    # Two 3/4 bars at 120 BPM (1.5 s each), one 4/4 bar (2 s), then 4/4 at 60 BPM (4 s).
    assert lines[:5] == [0.0, 1.5, 3.0, 5.0, 9.0]


def test_align_bars_with_a_meter_change():
    # Bar 1 in 3/4, bar 2 onwards 4/4; the performance drifts 60 ticks per bar.
    starts = [0, 480, 960, 1440, 1920, 2400, 2880, 3360, 3840]
    drift = [0, 0, 0, 60, 60, 60, 60, 120, 120]
    score = midi(pitches=[60] * 9, velocities=[64] * 9, starts=starts, lengths=[400] * 9)
    score.tracks[0].insert(1, mido.MetaMessage('time_signature', numerator=3, denominator=4, time=0))
    score.tracks[0].append(mido.MetaMessage('time_signature', numerator=4, denominator=4, time=1440))
    score.tracks[0] = mido.MidiTrack([m for m in score.tracks[0]
                                      if not (m.type == 'time_signature' and m.numerator == 4 and m.time == 0)])
    perf = midi(pitches=[60] * 9, velocities=[64] * 9, starts=[s + d for s, d in zip(starts, drift)], lengths=[400] * 9)
    out, _ = blend_performance(score, perf, bpm=120, strength=1.0, align='bars')
    got = [t for t, _ in _onsets(out)]
    for bar_line in (0, 1440, 3360):   # 3/4 bar, then 4/4 bars
        assert abs(got[starts.index(bar_line)] - bar_line) <= 1
