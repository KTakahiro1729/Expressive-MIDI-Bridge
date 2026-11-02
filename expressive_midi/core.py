"""MIDI timing and note-pair transformations.

Preserves channels, tracks, tempo/meter meta events and other controllers.
Input and output are standard type-0/1 MIDI files. Type 2 is rejected.
"""
from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
from bisect import bisect_right
import math
import random

import mido


@dataclass
class Note:
    track: int
    on_index: int
    off_index: int
    channel: int
    pitch: int
    velocity: int
    onset_tick: int
    offset_tick: int
    group: int = 0


class TempoMap:
    """Piecewise linear tick/second conversion for a MIDI tempo map."""
    def __init__(self, midi: mido.MidiFile):
        self.tpb = midi.ticks_per_beat
        raw = [(0, 500000, -1, -1)]
        for t, tr in enumerate(midi.tracks):
            tick = 0
            for i, msg in enumerate(tr):
                tick += msg.time
                if msg.type == 'set_tempo':
                    raw.append((tick, msg.tempo, t, i))
        raw.sort(key=lambda r: (r[0], r[2], r[3]))
        ticks, tempos = [], []
        for tick, tempo, _, _ in raw:
            if ticks and tick == ticks[-1]:
                tempos[-1] = tempo
            else:
                ticks.append(tick)
                tempos.append(tempo)
        seconds = [0.0]
        for i in range(1, len(ticks)):
            seconds.append(seconds[-1] + (ticks[i] - ticks[i-1]) * tempos[i-1] / (1_000_000 * self.tpb))
        self.ticks = ticks
        self.tempos = tempos
        self.seconds = seconds

    def to_seconds(self, tick: int | float) -> float:
        i = max(0, bisect_right(self.ticks, tick) - 1)
        return self.seconds[i] + (tick - self.ticks[i]) * self.tempos[i] / (1_000_000 * self.tpb)

    def to_ticks(self, seconds: float) -> int:
        seconds = max(0.0, seconds)
        i = max(0, bisect_right(self.seconds, seconds) - 1)
        return max(0, round(self.ticks[i] + (seconds - self.seconds[i]) * (1_000_000 * self.tpb) / self.tempos[i]))


def notes_from_midi(midi: mido.MidiFile) -> list[Note]:
    notes = []
    for tr_num, track in enumerate(midi.tracks):
        pending = defaultdict(deque)
        tick = 0
        for idx, msg in enumerate(track):
            tick += msg.time
            if msg.type == 'note_on' and msg.velocity > 0:
                pending[(msg.channel, msg.note)].append((idx, tick, msg.velocity))
            elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
                key = (msg.channel, msg.note)
                if pending[key]:
                    on_idx, on_tick, velocity = pending[key].popleft()
                    notes.append(Note(tr_num, on_idx, idx, msg.channel, msg.note,
                                      velocity, on_tick, tick))
    return notes


def render_baseline(midi: mido.MidiFile, *, strength: float = 0.65,
                    seed: int = 23, style: str = 'classical') -> mido.MidiFile:
    """Musically-informed *heuristic* (non-AI) baseline for A/B and plumbing tests.

    Chord tones share timing offsets. No pitches are added/removed. Sustain CC is
    unchanged. Tempo-map and other non-note events retain their original ticks.
    """
    if midi.type == 2:
        raise ValueError('MIDI type 2 is not supported (independent track clocks).')
    if not (0.0 <= strength <= 1.0):
        raise ValueError('strength must be between 0 and 1')
    if style not in {'classical', 'pop', 'jazz'}:
        raise ValueError('style must be classical, pop or jazz')
    # Identity is exact at strength zero, including EOT padding and note-off form.
    if strength == 0:
        return mido.MidiFile(file=None, type=midi.type, ticks_per_beat=midi.ticks_per_beat,
                             tracks=[mido.MidiTrack(msg.copy() for msg in tr) for tr in midi.tracks])

    timing = TempoMap(midi)
    notes = notes_from_midi(midi)
    rng = random.Random(seed)
    by_onset = sorted(notes, key=lambda n: (n.onset_tick, n.pitch, n.track))
    # Merge nearly simultaneous chord tones (max 12 ms), shared timing shift.
    groups: list[list[Note]] = []
    for note in by_onset:
        if not groups or timing.to_seconds(note.onset_tick) - timing.to_seconds(groups[-1][0].onset_tick) > .012:
            groups.append([])
        note.group = len(groups) - 1
        groups[-1].append(note)

    onset_changes = {}
    off_changes = {}
    for gi, group in enumerate(groups):
        first = group[0]
        beat = first.onset_tick / midi.ticks_per_beat
        pos = beat % 4
        phrase_position = (beat % 16) / 16
        # Small phrase arcs and metrical emphasis, not a trained performance model.
        arc = math.sin(math.pi * phrase_position)
        downbeat = math.cos(2 * math.pi * pos / 4)
        variance = rng.gauss(0, 1)
        max_shift_ms = {'classical': 22, 'pop': 10, 'jazz': 23}[style]
        shift_s = strength * max_shift_ms / 1000 * (0.6 * variance + 0.4 * math.sin(beat * 1.7))
        # Group start shares the shift; stable relative chord voicing.
        target_sec = max(0.0, timing.to_seconds(first.onset_tick) + shift_s)
        tick_delta = timing.to_ticks(target_sec) - first.onset_tick
        dynamic = strength * (10 * downbeat + 7 * arc + rng.gauss(0, 4))
        if style == 'jazz':
            dynamic += strength * (6 if pos >= 1.8 else -3)
        for note in group:
            new_on = max(0, note.onset_tick + tick_delta)
            vel = max(1, min(127, round(note.velocity + dynamic + rng.gauss(0, 2.5) * strength)))
            note_len_s = max(0.0, timing.to_seconds(note.offset_tick) - timing.to_seconds(note.onset_tick))
            # Preserve general articulation; don't lengthen until next same-pitch note.
            dur_factor = 1 + strength * (0.028 * (arc - 0.4) + rng.gauss(0, .018))
            new_off = max(new_on + 1, timing.to_ticks(timing.to_seconds(new_on) + max(.02, note_len_s * dur_factor)))
            onset_changes[(note.track, note.on_index)] = (new_on, vel)
            off_changes[(note.track, note.off_index)] = new_off

    # Prevent adjacent same-channel/pitch notes from overlapping after shifts.
    same_pitch = defaultdict(list)
    for n in notes:
        same_pitch[(n.track, n.channel, n.pitch)].append(n)
    for sequence in same_pitch.values():
        sequence.sort(key=lambda n: (n.onset_tick, n.on_index))
        for prev, nex in zip(sequence, sequence[1:]):
            a = (prev.track, prev.off_index)
            b = (nex.track, nex.on_index)
            if off_changes[a] >= onset_changes[b][0]:
                off_changes[a] = max(onset_changes[(prev.track, prev.on_index)][0] + 1,
                                     onset_changes[b][0] - 1)
                # If no room, shift next note forward.
                if off_changes[a] >= onset_changes[b][0]:
                    on, vel = onset_changes[b]
                    onset_changes[b] = (off_changes[a] + 1, vel)
                    off_changes[(nex.track, nex.off_index)] = max(off_changes[(nex.track, nex.off_index)], off_changes[a] + 2)

    out = mido.MidiFile(type=midi.type, ticks_per_beat=midi.ticks_per_beat)
    for ti, track in enumerate(midi.tracks):
        abs_tick = 0
        events = []
        ends = []
        for idx, original in enumerate(track):
            abs_tick += original.time
            if original.type == 'end_of_track':
                ends.append(abs_tick)
                continue
            msg = original.copy(time=0)
            if (ti, idx) in onset_changes:
                tick, vel = onset_changes[(ti, idx)]
                msg.velocity = vel
                rank = 2
            elif (ti, idx) in off_changes:
                tick = off_changes[(ti, idx)]
                rank = 0
            else:
                tick = abs_tick
                rank = 1
            events.append((tick, rank, idx, msg))
        events.sort(key=lambda row: (row[0], row[1], row[2]))
        newtrack = mido.MidiTrack()
        last = 0
        for tick, _, _, msg in events:
            newtrack.append(msg.copy(time=tick - last))
            last = tick
        newtrack.append(mido.MetaMessage('end_of_track', time=max(0, max(ends, default=last) - last)))
        out.tracks.append(newtrack)
    return out


def flatten_tempo(midi: mido.MidiFile, *, bpm: float = 120.0) -> mido.MidiFile:
    """Convert variable-tempo SMF to constant-tempo SMF at fixed wall-clock times.

    Useful when using a model-generated MIDI file with expressive tempo in a
    DAW session whose tempo must not be overwritten. The DAW project BPM MUST
    match this export BPM. The new file is not a beat-aligned score.
    """
    if midi.type == 2:
        raise ValueError('MIDI type 2 is unsupported')
    if not math.isfinite(bpm) or not 20 <= bpm <= 300:
        raise ValueError('bpm must be finite and within 20..300')
    tm = TempoMap(midi)
    out = mido.MidiFile(type=midi.type, ticks_per_beat=midi.ticks_per_beat)
    target_tempo = round(60_000_000 / bpm)
    for index, track in enumerate(midi.tracks):
        events = []
        abs_tick = 0
        for i, msg in enumerate(track):
            abs_tick += msg.time
            if msg.type in {'set_tempo', 'end_of_track'}:
                continue
            new_tick = max(0, round(tm.to_seconds(abs_tick) * bpm / 60 * midi.ticks_per_beat))
            events.append((new_tick, i, msg))
        events.sort(key=lambda r: (r[0], r[1]))
        dst = mido.MidiTrack()
        last = 0
        if index == 0:
            dst.append(mido.MetaMessage('set_tempo', tempo=target_tempo, time=0))
        for tick, i, msg in events:
            dst.append(msg.copy(time=tick-last))
            last = tick
        dst.append(mido.MetaMessage('end_of_track', time=0))
        out.tracks.append(dst)
    return out
