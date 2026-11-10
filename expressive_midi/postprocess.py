"""Combine AI performance with the original score, without modifying the sound source.

A score and a model performance are normalized to the same constant BPM before
matching notes. This is an *offline* processor; it makes no host/API assumptions.
"""
from __future__ import annotations

from bisect import bisect_right
from collections import Counter, defaultdict
from dataclasses import dataclass
from math import isfinite
from statistics import median
import mido

from .core import TempoMap, flatten_tempo, notes_from_midi


@dataclass(frozen=True)
class RenderStats:
    notes: int
    velocity_changed: int
    onset_changed: int
    duration_changed: int
    pedal_events: int


def _rebuild(midi: mido.MidiFile, note_on, note_off, *, pedal_events, replace_pedal):
    """Rebuild tracks in absolute ticks; preserve unrelated MIDI messages."""
    out = mido.MidiFile(type=midi.type, ticks_per_beat=midi.ticks_per_beat)
    for tr_no, track in enumerate(midi.tracks):
        tick = 0
        end = 0
        events = []
        for idx, msg in enumerate(track):
            tick += msg.time
            if msg.type == 'end_of_track':
                end = max(end, tick)
                continue
            if replace_pedal and msg.type == 'control_change' and msg.control == 64:
                continue
            key = (tr_no, idx)
            if key in note_on:
                new_tick, velocity = note_on[key]
                msg = msg.copy(velocity=velocity)
                rank = 3
            elif key in note_off:
                new_tick = note_off[key]
                rank = 1
            else:
                new_tick = tick
                rank = 2
            events.append((new_tick, rank, idx, msg.copy(time=0)))
            end = max(end, new_tick)
        if tr_no in pedal_events:
            for j, (ptick, channel, value) in enumerate(pedal_events[tr_no]):
                events.append((ptick, 0, j,
                               mido.Message('control_change', channel=channel, control=64, value=value)))
        events.sort(key=lambda e: (e[0], e[1], e[2]))
        dst = mido.MidiTrack()
        last = 0
        for at, _, _, msg in events:
            dst.append(msg.copy(time=at-last))
            last = at
        dst.append(mido.MetaMessage('end_of_track', time=max(0, end-last)))
        out.tracks.append(dst)
    return out


def _match_notes(source_notes, target_notes, tm_original):
    """Pair every score note with a model note: per pitch, in time order.

    Same-pitch notes that start at the same millisecond (unisons written in two
    voices) form one group: Pianist Transformer's preprocessing (normalize_midi,
    500 ticks/beat at 120 BPM) keeps only one of them. Every member of a group is
    paired with that single model note. Any other mismatch raises.
    """
    def by_pitch(notes):
        groups = defaultdict(list)
        for note in notes:
            groups[note.pitch].append(note)
        for group in groups.values():
            group.sort(key=lambda n: (n.onset_tick, n.track, n.on_index))
        return groups

    def unison_groups(notes):
        groups = []
        for note in notes:
            key = round(tm_original.to_seconds(note.onset_tick) * 1000)
            if groups and groups[-1][0] == key:
                groups[-1][1].append(note)
            else:
                groups.append((key, [note]))
        return [members for _, members in groups]

    source_groups = {pitch: unison_groups(notes) for pitch, notes in by_pitch(source_notes).items()}
    target_by_pitch = by_pitch(target_notes)
    c1 = Counter({pitch: len(groups) for pitch, groups in source_groups.items()})
    c2 = Counter(n.pitch for n in target_notes)
    if c1 != c2:
        diff = {p: (c1.get(p, 0), c2.get(p, 0)) for p in sorted(set(c1) | set(c2)) if c1.get(p, 0) != c2.get(p, 0)}
        raise ValueError(f'Model output notes differ from the score at {len(diff)} pitch(es) '
                         f'(pitch: score, model): {diff}')
    return [(a, b) for pitch, groups in source_groups.items()
            for group, b in zip(groups, target_by_pitch[pitch]) for a in group]


def _bar_length_seconds(midi: mido.MidiFile, bpm: float) -> float:
    for track in midi.tracks:
        for msg in track:
            if msg.type == 'time_signature':
                return msg.numerator * 4 / msg.denominator * 60 / bpm
    return 4 * 60 / bpm


def bar_anchors(pairs, bar: float, beat: float) -> tuple[list[float], list[float]]:
    """Score bar-line times and where the performance is at each of them.

    The performance position at a bar line is the median offset of the notes the
    score puts on that downbeat, or of the notes within half a beat of it when the
    downbeat is silent. Bars without nearby notes are interpolated. The result is
    strictly increasing, so the warp it defines keeps the note order.
    """
    if not pairs:
        return [], []
    last = max(a for a, _ in pairs)
    count = int(last // bar) + 2
    score_t = [k * bar for k in range(count)]
    offsets: list[float | None] = []
    for t in score_t:
        exact = [b - a for a, b in pairs if abs(a - t) < 0.02]
        near = exact or [b - a for a, b in pairs if abs(a - t) <= beat / 2]
        offsets.append(median(near) if near else None)
    known = [k for k, d in enumerate(offsets) if d is not None]
    if not known:
        return [], []
    for k, d in enumerate(offsets):
        if d is None:
            before = max((j for j in known if j < k), default=None)
            after = min((j for j in known if j > k), default=None)
            if before is None:
                offsets[k] = offsets[after]
            elif after is None:
                offsets[k] = offsets[before]
            else:
                w = (k - before) / (after - before)
                offsets[k] = offsets[before] * (1 - w) + offsets[after] * w
    perf_t = [t + d for t, d in zip(score_t, offsets)]
    for k in range(1, len(perf_t)):
        perf_t[k] = max(perf_t[k], perf_t[k - 1] + 0.2 * bar)
    return score_t, perf_t


def _warp(sec: float, perf_t: list[float], score_t: list[float]) -> float:
    """Piecewise-linear map from performance time to score time through the anchors."""
    if sec <= perf_t[0]:
        return score_t[0] + (sec - perf_t[0])
    if sec >= perf_t[-1]:
        return score_t[-1] + (sec - perf_t[-1])
    k = bisect_right(perf_t, sec) - 1
    span = perf_t[k + 1] - perf_t[k]
    return score_t[k] + (sec - perf_t[k]) * (score_t[k + 1] - score_t[k]) / span


def align_to_bars(original: mido.MidiFile, performed: mido.MidiFile, pairs_sec, bpm: float) -> mido.MidiFile:
    """Return the performance with its tempo drift removed bar by bar.

    Every bar line of the performance is moved onto the score's bar line; inside a
    bar the model's timing (rubato, chord spread, micro-timing) and its pedal are
    stretched along with it. Both files must already have the same constant tempo.
    """
    bar = _bar_length_seconds(original, bpm)
    score_t, perf_t = bar_anchors(pairs_sec, bar, 60 / bpm)
    if not score_t:
        return performed
    tm = TempoMap(performed)
    tps = performed.ticks_per_beat * bpm / 60
    out = mido.MidiFile(type=performed.type, ticks_per_beat=performed.ticks_per_beat)
    for track in performed.tracks:
        tick, events, end = 0, [], 0
        for index, msg in enumerate(track):
            tick += msg.time
            if msg.type == 'end_of_track':
                end = tick
                continue
            new = tick if msg.is_meta else max(0, round(_warp(tm.to_seconds(tick), perf_t, score_t) * tps))
            events.append((new, index, msg))
        events.sort(key=lambda e: (e[0], e[1]))
        rebuilt, last = mido.MidiTrack(), 0
        for at, _, msg in events:
            rebuilt.append(msg.copy(time=at - last))
            last = at
        end = max(round(_warp(tm.to_seconds(end), perf_t, score_t) * tps), last)
        rebuilt.append(mido.MetaMessage('end_of_track', time=end - last))
        out.tracks.append(rebuilt)
    return out


def blend_performance(score: mido.MidiFile, performance: mido.MidiFile,
                      *, bpm: float = 120.0, strength: float = 1.0,
                      pedal: str = 'model', align: str = 'none') -> tuple[mido.MidiFile, RenderStats]:
    """Blend model performance into score MIDI, preserving note pitch/channel/track.

    The two files may have different TPB and tempo maps. Notes are matched by
    pitch and order of occurrence (see _match_notes). Output has a *constant*
    tempo set by bpm for DAW playback.

    pedal: model (replace original CC64), score (keep original), or none.
    align: none (keep the model's tempo, so the music drifts off the DAW grid) or
    bars (pull the performance back onto every bar line of the score, keeping the
    expression inside each bar, so it lines up with other tracks).
    """
    if not isfinite(strength) or not 0 <= strength <= 1:
        raise ValueError('strength must be finite and in [0, 1]')
    if pedal not in {'model', 'score', 'none'}:
        raise ValueError('pedal must be model, score or none')
    if align not in {'none', 'bars'}:
        raise ValueError('align must be none or bars')
    # Validates bpm, input type; preserves original channel/program/metadata.
    original = flatten_tempo(score, bpm=bpm)
    performed = flatten_tempo(performance, bpm=bpm)
    tm_original, tm_perf = TempoMap(original), TempoMap(performed)
    source_notes, target_notes = notes_from_midi(original), notes_from_midi(performed)
    if not source_notes:
        raise ValueError('No matching piano notes in score')
    pairs = _match_notes(source_notes, target_notes, tm_original)
    if align == 'bars':
        seconds = [(tm_original.to_seconds(a.onset_tick), tm_perf.to_seconds(b.onset_tick)) for a, b in pairs]
        performed = align_to_bars(original, performed, seconds, bpm)
        tm_perf = TempoMap(performed)
        pairs = _match_notes(source_notes, notes_from_midi(performed), tm_original)

    changes_on, changes_off = {}, {}
    stats_on = stats_v = stats_duration = 0
    tpb = original.ticks_per_beat
    ticks_per_second = tpb * bpm / 60
    # No negative ticks; an AI may start early. Preserve the negative shift where possible.
    for a, b in pairs:
        a_s = tm_original.to_seconds(a.onset_tick)
        b_s = tm_perf.to_seconds(b.onset_tick)
        a_e = tm_original.to_seconds(a.offset_tick)
        b_e = tm_perf.to_seconds(b.offset_tick)
        start_sec = max(0.0, (1-strength)*a_s + strength*b_s)
        end_sec = (1-strength)*a_e + strength*b_e
        onset = max(0, round(start_sec * ticks_per_second))
        offset = max(onset+1, round(end_sec * ticks_per_second))
        velocity = max(1, min(127, round((1-strength)*a.velocity + strength*b.velocity)))
        changes_on[(a.track, a.on_index)] = (onset, velocity)
        changes_off[(a.track, a.off_index)] = offset
        stats_on += onset != a.onset_tick
        stats_v += velocity != a.velocity
        stats_duration += (offset-onset) != (a.offset_tick-a.onset_tick)

    # Limit retrigger overlaps of the same pitch/channel on the same track.
    overlapping = defaultdict(list)
    for n in source_notes:
        overlapping[n.track, n.channel, n.pitch].append(n)
    for ns in overlapping.values():
        ns.sort(key=lambda n: (changes_on[n.track, n.on_index][0], n.on_index))
        for prev, nxt in zip(ns, ns[1:]):
            end_key = (prev.track, prev.off_index)
            next_on = changes_on[nxt.track, nxt.on_index][0]
            prev_on = changes_on[prev.track, prev.on_index][0]
            # If simultaneous same-pitch retrigger, move second note ahead.
            if next_on <= prev_on:
                _, vel = changes_on[nxt.track, nxt.on_index]
                next_on = prev_on + 2
                changes_on[nxt.track, nxt.on_index] = (next_on, vel)
                changes_off[nxt.track, nxt.off_index] = max(changes_off[nxt.track, nxt.off_index], next_on + 1)
            if changes_off[end_key] >= next_on:
                changes_off[end_key] = max(prev_on+1, next_on-1)

    extra = defaultdict(list)
    if pedal == 'model' and strength > 0:
        # AI's CC64 is explicitly authored to the channel/track of the source.
        dest = source_notes[0]
        tick = 0
        for tr in performed.tracks:
            tick = 0
            for msg in tr:
                tick += msg.time
                if msg.type == 'control_change' and msg.control == 64:
                    sec = tm_perf.to_seconds(tick)
                    extra[dest.track].append((max(0, round(sec*ticks_per_second)),
                                               dest.channel, msg.value))
        if extra:
            extra[dest.track].sort()
    if pedal == 'score' or (pedal == 'model' and strength == 0):
        replace = False
    else:
        replace = True
    if pedal == 'none':
        extra.clear()
    out = _rebuild(original, changes_on, changes_off,
                   pedal_events=extra, replace_pedal=replace)
    return out, RenderStats(len(source_notes), stats_v, stats_on, stats_duration,
                            sum(len(v) for v in extra.values()))
