"""Combine AI performance with the original score, without modifying the sound source.

A score and a model performance are normalized to the same constant BPM before
matching notes. This is an *offline* processor; it makes no host/API assumptions.
"""
from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
from math import isfinite
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


def blend_performance(score: mido.MidiFile, performance: mido.MidiFile,
                      *, bpm: float = 120.0, strength: float = 1.0,
                      pedal: str = 'model') -> tuple[mido.MidiFile, RenderStats]:
    """Blend model performance into score MIDI, preserving note pitch/channel/track.

    The two files may have different TPB and tempo maps. Notes are matched by
    pitch and order of occurrence. A mismatch raises rather than corrupting
    the music. Output has a *constant* tempo set by bpm for DAW playback.

    pedal: model (replace original CC64), score (keep original), or none.
    """
    if not isfinite(strength) or not 0 <= strength <= 1:
        raise ValueError('strength must be finite and in [0, 1]')
    if pedal not in {'model', 'score', 'none'}:
        raise ValueError('pedal must be model, score or none')
    # Validates bpm, input type; preserves original channel/program/metadata.
    original = flatten_tempo(score, bpm=bpm)
    performed = flatten_tempo(performance, bpm=bpm)
    tm_original, tm_perf = TempoMap(original), TempoMap(performed)
    source_notes, target_notes = notes_from_midi(original), notes_from_midi(performed)
    c1, c2 = Counter(n.pitch for n in source_notes), Counter(n.pitch for n in target_notes)
    if c1 != c2:
        raise ValueError(f'Model output note pitches/counts differ: score={dict(c1)}, model={dict(c2)}')
    if not source_notes:
        raise ValueError('No matching piano notes in score')

    def by_pitch(notes):
        pairs = defaultdict(list)
        for note in notes:
            pairs[note.pitch].append(note)
        for group in pairs.values():
            group.sort(key=lambda n: (n.onset_tick, n.track, n.on_index))
        return pairs

    source_by_pitch, target_by_pitch = by_pitch(source_notes), by_pitch(target_notes)
    changes_on, changes_off = {}, {}
    stats_on = stats_v = stats_duration = 0
    tpb = original.ticks_per_beat
    ticks_per_second = tpb * bpm / 60
    # No negative ticks; an AI may start early. Preserve the negative shift where possible.
    for pitch, notes in source_by_pitch.items():
        for a, b in zip(notes, target_by_pitch[pitch]):
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
