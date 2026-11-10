#!/usr/bin/env python3
"""Convert Tetrisphere "DCM1" songs to standard MIDI files, one track per channel.

The song data is read from the player's own ROM at run time; nothing derived
from the ROM is stored in this file or in the repository. Write the output to
.local/ (git-ignored), for example:

    python3 -I tools/audio/dcm_to_midi.py --rom .local/m0/tetrisphere-us.z64 \
        --song 297 --out .local/music/song_297.mid
    python3 -I tools/audio/dcm_to_midi.py --rom .local/m0/tetrisphere-us.z64 \
        --all-intro --out-dir .local/music

Format summary (see docs/research/blockplanet-game-state.md, "DCM1 song
format"): a DCM1 song is a recording of a tracker player's output, not a
pattern module. After a small header and an instrument table comes an
LZ-compressed byte stream that holds, for every player tick and every voice
in turn, either nothing (run-length skips) or a set of register changes:
pitch (playback rate in Hz), volume, pan, instrument, sample offset, key-on
and tempo. This tool replays that stream the way func_8007B124 does and turns
key-ons into MIDI notes.
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ROM layout facts (addresses, not data).
SONG_TABLE_ROM = 0xCD7C0          # u16 count, then count + 1 u32 offsets
IMAGE_ROM = 0x1000                # main code/data image starts here in ROM ...
# ... and is loaded at the header's entry point (0x80025C50 for this game).
PITCH_EXP_TABLE_VADDR = 0x800E2850   # 4 x u32 bases used by the pitch encoding
PITCH_CONST_VADDR = 0x800EE440       # 2 x f64 used by func_8007E36C
PITCH_CLOCK_INSN_VADDR = 0x8007E394  # lui/ori pair holding the clock constant

INTRO_SONGS = (297, 308, 0)

MIDI_DIVISION = 96          # MIDI ticks per quarter note
MIDI_TICKS_PER_GAME_TICK = 4  # so a quarter note is 24 player ticks (speed 6, 4 rows)
DEFAULT_TICK_US = 20000     # player default before the song sets a tempo (125 BPM)
BEND_RANGE = 12             # semitones, set with RPN 0 on every track


# --------------------------------------------------------------------------
# ROM access


class Rom:
    def __init__(self, data: bytes):
        if data[:4] != b"\x80\x37\x12\x40":
            raise SystemExit("expected a big-endian (.z64) ROM")
        self.data = data
        self.entry = struct.unpack(">I", data[8:12])[0]

    def vaddr(self, address: int, size: int) -> bytes:
        offset = address - self.entry + IMAGE_ROM
        if not IMAGE_ROM <= offset <= len(self.data) - size:
            raise SystemExit(f"address {address:#x} is outside the ROM image")
        return self.data[offset:offset + size]

    def song(self, index: int) -> bytes:
        count = struct.unpack(">H", self.data[SONG_TABLE_ROM:SONG_TABLE_ROM + 2])[0]
        if not 0 <= index < count:
            raise SystemExit(f"song {index} out of range (table has {count})")
        at = SONG_TABLE_ROM + 2 + 4 * index
        start, end = struct.unpack(">II", self.data[at:at + 8])
        blob = self.data[SONG_TABLE_ROM + start:SONG_TABLE_ROM + end]
        if blob[:4] != b"DCM1":
            raise SystemExit(f"song {index} does not start with DCM1")
        return blob


@dataclass
class PitchModel:
    """Mirror of the 16-bit pitch word decode and func_8007E36C."""

    exp_bases: tuple[int, int, int, int]
    ref_period: float   # period that plays a sample at its reference rate
    ref_rate: float     # that reference rate in Hz
    clock: int

    @classmethod
    def from_rom(cls, rom: Rom) -> "PitchModel":
        bases = struct.unpack(">4I", rom.vaddr(PITCH_EXP_TABLE_VADDR, 16))
        ref_period, ref_rate = struct.unpack(">2d", rom.vaddr(PITCH_CONST_VADDR, 16))
        lui, ori = struct.unpack(">2I", rom.vaddr(PITCH_CLOCK_INSN_VADDR, 8))
        if lui >> 26 != 0x0F or ori >> 26 != 0x0D:
            raise SystemExit("unexpected code at the pitch routine; wrong ROM?")
        clock = ((lui & 0xFFFF) << 16) | (ori & 0xFFFF)
        return cls(bases, ref_period, ref_rate, clock)

    def value(self, word: int) -> int:
        """The u16 pitch word: top 2 bits pick a base and a shift."""
        exp = word >> 14
        return (self.exp_bases[exp] + ((word & 0x3FFF) << exp)) & 0xFFFF

    def rate_hz(self, word: int) -> float:
        value = self.value(word)
        if value <= 0:
            return 0.0
        period = self.clock // value
        if period == 0:
            return 0.0
        return self.ref_period / period * self.ref_rate

    def note(self, word: int) -> float | None:
        """MIDI note, with the reference rate written as middle C (60)."""
        rate = self.rate_hz(word)
        if rate <= 0:
            return None
        return 60.0 + 12.0 * math.log2(rate / self.ref_rate)


# --------------------------------------------------------------------------
# DCM1 parsing


@dataclass
class Instrument:
    index: int
    length: int       # sample frames
    loop_start: int
    loop_end: int
    flags: int        # bit 2: looped (loop set up by func_8007A088)
    sample_id: int    # id handed to the sample loader func_8007AC44

    @property
    def looped(self) -> bool:
        return bool(self.flags & 4)


@dataclass
class Song:
    index: int
    voices: int
    instruments: list[Instrument]
    stream: bytes


def lz_window() -> bytearray:
    """Initial dictionary built by func_80079660."""
    w = bytearray(0x1000)
    for i in range(0x100):
        w[i] = i
        w[0x100 + i] = 0xFF - i
        w[0x200 + 4 * i:0x204 + 4 * i] = bytes((i,)) * 4
    for i in range(0x600, 0x1000):
        w[i] = i & 0xFF
    return w


def lz_decompress(src: bytes, size: int) -> bytes:
    """Byte-at-a-time LZ of func_8007979C (4 KiB window, absolute offsets)."""
    window = lz_window()
    wp = 0
    p = 0
    out = bytearray()
    while len(out) < size and p < len(src):
        c = src[p]
        p += 1
        if c & 0xF0 == 0:                     # literal run of c + 1 bytes
            for _ in range(c + 1):
                b = src[p]
                p += 1
                window[wp] = b
                wp = (wp + 1) & 0xFFF
                out.append(b)
        else:                                 # copy (c >> 4) + 2 bytes
            off = ((c & 0x0F) << 8) | src[p]
            p += 1
            for _ in range((c >> 4) + 2):
                b = window[off]
                off = (off + 1) & 0xFFF
                window[wp] = b
                wp = (wp + 1) & 0xFFF
                out.append(b)
    if len(out) < size:
        raise SystemExit("compressed stream ended early")
    return bytes(out[:size])


def parse_song(index: int, blob: bytes) -> Song:
    voices, count = blob[4], blob[5]
    packed_size = struct.unpack("<I", blob[6:10])[0]      # includes the BE size word
    pos = 14
    instruments = []
    for i in range(count):
        length, loop_start, loop_end, flags, sample_id = struct.unpack(
            "<IIIHH", blob[pos:pos + 16])
        instruments.append(Instrument(i, length, loop_start, loop_end, flags, sample_id))
        pos += 16
    size = struct.unpack(">I", blob[pos:pos + 4])[0]
    packed = blob[pos + 4:pos + packed_size]
    return Song(index, voices, instruments, lz_decompress(packed, size))


# --------------------------------------------------------------------------
# Replay


@dataclass
class Event:
    tick: int        # player tick
    voice: int
    kind: str        # pitch, vol, pan, inst, offset, key, tempo
    value: int


def replay(song: Song):
    """Yield (tick, tick_us, events) the way func_8007B124 reads the stream."""
    data = song.stream
    p = 0
    skip = 0
    tick = 0
    tick_us = DEFAULT_TICK_US
    while p < len(data):
        events = []
        for v in range(song.voices):
            if skip:
                skip -= 1
                continue
            if p >= len(data):
                break
            s = data[p]
            p += 1
            if s & 0x80:                     # this slot and the next s & 0x7F are empty
                skip = s & 0x7F
                continue
            if s & 0x40:
                events.append(Event(tick, v, "pitch", data[p] | data[p + 1] << 8))
                p += 2
            if s & 0x20:
                events.append(Event(tick, v, "vol", data[p] or 1))
                p += 1
            if s & 0x10:
                events.append(Event(tick, v, "pan", data[p] >> 1))
                p += 1
            if s & 0x08:
                events.append(Event(tick, v, "inst", data[p]))
                p += 1
            if s & 0x04:
                events.append(Event(tick, v, "offset", data[p] << 8))
                p += 1
            if s & 0x02:
                events.append(Event(tick, v, "key", 1))
            if s & 0x01:
                ext = data[p]
                p += 1
                if ext & 0x40:
                    tempo = data[p]
                    p += 1
                    if tempo:
                        tick_us = 125_000_000 // (tempo * 50)
                        events.append(Event(tick, v, "tempo", tempo))
                if ext & 0x20:
                    p += 1                   # read and ignored by the player
        yield tick, tick_us, events
        tick += 1


# --------------------------------------------------------------------------
# MIDI building


def vlq(n: int) -> bytes:
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def meta(kind: int, payload: bytes) -> bytes:
    return b"\xFF" + bytes((kind,)) + vlq(len(payload)) + payload


def vol_to_midi(vol: int) -> int:
    """Game amplitude is sqrt(vol); MIDI amplitude is about (x / 127) ** 2."""
    return max(1, min(127, round(127 * (vol / 255) ** 0.25)))


@dataclass
class VoiceState:
    inst: int | None = None
    pitch_word: int = 0
    vol: int = 0
    pan: int = 64
    sounding: bool = False
    note: int = 0
    pending: tuple = ()          # (start time, key, [(time, volume)])
    bend: int = 8192
    expr: int = 127
    program: int | None = None
    frames_left: float = 0.0     # for one-shot samples
    offset: int = 0              # pending sample start offset
    # statistics
    notes: int = 0
    instruments: dict = field(default_factory=dict)
    pitches: set = field(default_factory=set)
    first: float | None = None
    last_end: float = 0.0
    total_on: float = 0.0
    on_since: float = 0.0
    one_shot_ends: int = 0


class TrackWriter:
    def __init__(self, channel: int):
        self.channel = channel
        self.events: list[tuple[int, int, bytes]] = []   # (time, order, bytes)

    def add(self, time: int, data: bytes, order: int = 1):
        self.events.append((time, order, data))

    def ch(self, time: int, status: int, *data: int, order: int = 1):
        self.add(time, bytes((status | self.channel, *data)), order)

    def encode(self) -> bytes:
        body = bytearray()
        now = 0
        for time, _, data in sorted(self.events, key=lambda e: (e[0], e[1])):
            body += vlq(time - now) + data
            now = time
        body += vlq(0) + meta(0x2F, b"")
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def convert(song: Song, pitch: PitchModel, drum_voice: int | None = None,
            transpose: int = 0):
    nv = song.voices
    channels = []
    free = [c for c in range(16) if c != 9]
    for v in range(nv):
        if v == drum_voice:
            channels.append(9)
        else:
            channels.append(free.pop(0) if free else 9)
    tracks = [TrackWriter(channels[v]) for v in range(nv)]
    state = [VoiceState() for _ in range(nv)]
    conductor = TrackWriter(0)
    conductor.add(0, meta(0x03, f"DCM1 song {song.index}".encode()), 0)
    conductor.add(0, meta(0x58, bytes((4, 2, 24, 8))), 0)
    last_tempo_us = None
    seconds = 0.0

    for tr in tracks:
        tr.ch(0, 0xB0, 101, 0, order=0)
        tr.ch(0, 0xB0, 100, 0, order=0)
        tr.ch(0, 0xB0, 6, BEND_RANGE, order=0)
        tr.ch(0, 0xB0, 38, 0, order=0)
        tr.ch(0, 0xB0, 7, 127, order=0)

    def note_off(v, t, now_s):
        st = state[v]
        if st.sounding:
            # Velocity comes from the note's loudest volume; CC 11 follows
            # the volume envelope relative to that (many notes start at 0
            # and are faded in by later volume writes).
            start, key, vols = st.pending
            peak = max(vol for _, vol in vols)
            tracks[v].ch(start, 0x90, key, vol_to_midi(peak), order=2)
            for when, vol in vols:
                set_expr(v, when, max(0, min(127, round(127 * (vol / peak) ** 0.25))))
            tracks[v].ch(t, 0x80, key, 64, order=0)
            st.sounding = False
            st.total_on += now_s - st.on_since
            st.last_end = now_s

    def bend_for(delta: float) -> int:
        return max(0, min(16383, round(8192 + delta / BEND_RANGE * 8192)))

    def set_bend(v, t, value, order=1):
        st = state[v]
        if value != st.bend:
            tracks[v].ch(t, 0xE0, value & 0x7F, value >> 7, order=order)
            st.bend = value

    def set_expr(v, t, value):
        st = state[v]
        if value != st.expr:
            tracks[v].ch(t, 0xB0, 11, value)
            st.expr = value

    def note_on(v, t, now_s):
        st = state[v]
        n = pitch.note(st.pitch_word)
        if n is None or st.inst is None:
            return
        n += transpose
        key = max(0, min(127, round(n)))
        if st.inst != st.program:
            tracks[v].ch(t, 0xC0, st.inst & 0x7F, order=0)
            ins = song.instruments[st.inst] if st.inst < len(song.instruments) else None
            label = (f"inst {st.inst} sample {ins.sample_id:#x}" if ins
                     else f"inst {st.inst} (not in table)")
            tracks[v].add(t, meta(0x01, label.encode()), 0)
            st.program = st.inst
        set_bend(v, t, bend_for(n - key), order=0)
        st.pending = (t, key, [(t, st.vol)])
        st.sounding = True
        st.note = key
        st.on_since = now_s
        st.notes += 1
        st.instruments[st.inst] = st.instruments.get(st.inst, 0) + 1
        st.pitches.add(key)
        if st.first is None:
            st.first = now_s
        ins = song.instruments[st.inst] if st.inst < len(song.instruments) else None
        st.frames_left = (ins.length - st.offset) if ins and not ins.looped else math.inf

    tick_us = DEFAULT_TICK_US
    for tick, new_tick_us, events in replay(song):
        t = tick * MIDI_TICKS_PER_GAME_TICK
        # one-shot samples that ran out during the previous tick
        for v, st in enumerate(state):
            if st.sounding and st.frames_left <= 0:
                note_off(v, t, seconds)
                st.one_shot_ends += 1
        by_voice: dict[int, list[Event]] = {}
        for e in events:
            by_voice.setdefault(e.voice, []).append(e)
        for v, evs in by_voice.items():
            st = state[v]
            kinds = {e.kind for e in evs}
            keyed = "key" in kinds
            for e in evs:
                if e.kind == "pitch":
                    st.pitch_word = e.value
                    if st.sounding and not keyed:
                        n = pitch.note(e.value)
                        if n is None:
                            note_off(v, t, seconds)
                        else:
                            n += transpose
                            if abs(n - st.note) <= BEND_RANGE:
                                set_bend(v, t, bend_for(n - st.note))
                            else:   # too far for the bend range: restart the note
                                vol = st.vol
                                note_off(v, t, seconds)
                                left = st.frames_left
                                note_on(v, t, seconds)
                                st.notes -= 1
                                st.frames_left = left
                                st.vol = vol
                elif e.kind == "vol":
                    st.vol = e.value
                    if st.sounding and not keyed:
                        st.pending[2].append((t, e.value))
                elif e.kind == "pan":
                    if e.value != st.pan or tick == 0:
                        tracks[v].ch(t, 0xB0, 10, e.value, order=0)
                    st.pan = e.value
                elif e.kind == "inst":
                    st.inst = e.value
                    note_off(v, t, seconds)   # the player stops a busy voice here
                elif e.kind == "offset":
                    st.offset = e.value
                    note_off(v, t, seconds)
                elif e.kind == "tempo":
                    pass
            if keyed:
                note_off(v, t, seconds)
                note_on(v, t, seconds)
                st.offset = 0
        if new_tick_us != last_tempo_us:
            # the value returned after this tick is the time to the next tick
            us_per_quarter = new_tick_us * MIDI_DIVISION // MIDI_TICKS_PER_GAME_TICK
            conductor.add(t, meta(0x51, us_per_quarter.to_bytes(3, "big")), 0)
            last_tempo_us = new_tick_us
        tick_us = new_tick_us
        dt = tick_us / 1e6
        for st in state:
            if st.sounding and st.frames_left != math.inf:
                st.frames_left -= pitch.rate_hz(st.pitch_word) * dt
        seconds += dt
        end_tick = tick + 1

    t_end = end_tick * MIDI_TICKS_PER_GAME_TICK
    for v in range(nv):
        note_off(v, t_end, seconds)

    names = []
    for v, (tr, st) in enumerate(zip(tracks, state)):
        used = ", ".join(
            f"{i}:{song.instruments[i].sample_id:#x}" if i < len(song.instruments) else f"{i}:?"
            for i in sorted(st.instruments))
        name = f"ch {v} (instrument:sample id {used or 'none'})"
        tr.add(0, meta(0x03, name.encode()), -1)
        names.append(name)

    out = b"MThd" + struct.pack(">IHHH", 6, 1, nv + 1, MIDI_DIVISION)
    out += conductor.encode()
    for tr in tracks:
        out += tr.encode()
    return out, seconds, end_tick, state, channels


def describe(song: Song, seconds: float, ticks: int, state, channels) -> str:
    lines = [f"song {song.index}: {song.voices} channels, {len(song.instruments)} instruments, "
             f"{ticks} ticks, {seconds:.1f} s"]
    for v, st in enumerate(state):
        if not st.notes:
            lines.append(f"  ch {v:2d} (midi {channels[v] + 1:2d}): no notes")
            continue
        insts = ", ".join(
            f"{i}(s{song.instruments[i].sample_id:#x}{'' if song.instruments[i].looped else ' one-shot'})x{c}"
            if i < len(song.instruments) else f"{i}(?)x{c}"
            for i, c in sorted(st.instruments.items()))
        span = f"{st.first:6.1f}-{st.last_end:6.1f} s"
        lines.append(
            f"  ch {v:2d} (midi {channels[v] + 1:2d}): {st.notes:4d} notes, "
            f"{len(st.pitches):2d} keys {min(st.pitches)}-{max(st.pitches)}, {span}, "
            f"sounding {st.total_on:6.1f} s, inst {insts}")
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--song", type=int, help="song table index")
    ap.add_argument("--out", type=Path, help="output .mid for --song")
    ap.add_argument("--all-intro", action="store_true",
                    help="convert songs 297 and 308 (intro) and 0 (title) for comparison")
    ap.add_argument("--out-dir", type=Path, default=Path(".local/music"))
    ap.add_argument("--drum-voice", type=int, help="put this DCM channel on MIDI channel 10")
    ap.add_argument("--transpose", type=int, default=0, help="semitones added to every note")
    ap.add_argument("--quiet", action="store_true", help="do not print the channel summary")
    args = ap.parse_args(argv)

    rom = Rom(args.rom.read_bytes())
    pitch = PitchModel.from_rom(rom)
    jobs = []
    if args.all_intro:
        jobs += [(i, args.out_dir / f"song_{i}.mid") for i in INTRO_SONGS]
    if args.song is not None:
        jobs.append((args.song, args.out or args.out_dir / f"song_{args.song}.mid"))
    if not jobs:
        ap.error("give --song or --all-intro")

    for index, path in jobs:
        song = parse_song(index, rom.song(index))
        data, seconds, ticks, state, channels = convert(
            song, pitch, args.drum_voice, args.transpose)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if not args.quiet:
            print(describe(song, seconds, ticks, state, channels))
            print(f"  wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
