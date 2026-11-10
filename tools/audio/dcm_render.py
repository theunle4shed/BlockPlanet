#!/usr/bin/env python3
"""Render Tetrisphere "DCM1" songs to WAV with the game's own samples.

Research tool. Everything it reads comes from the player's own ROM at run
time and everything it writes must stay in .local/ (AGENTS.md, Game-derived
content). Needs numpy.

    python3 -I tools/audio/dcm_render.py --rom .local/m0/tetrisphere-us.z64 \
        --song 297 --voices --stems --out-dir .local/music

Writes song_N_mix.wav, and with --voices song_N_chNN.wav per voice, and with
--stems song_N_effects.wav / song_N_music.wav (split by sample id: see
--effects-samples). All files of one song share one scale, so the voices and
the stems add up to the mix.

What is modelled (see docs/research/blockplanet-game-state.md):
- samples: entry `sample id` of the sample bank (same table layout as the
  song table). Instrument flags bit 0 clear: the entry is packed with passes
  "S" (the 4 KiB LZ) then "D" (out[i] = out[i-1] - in[i]) and holds signed
  8-bit samples. Bit 0 set: no passes, signed 16-bit little-endian samples.
  func_8007A088 keeps looped samples (flags bit 2) as 16-bit and stores
  one-shots as 8-bit (16-bit sources lose their low byte).
- replay: the event stream of dcm_to_midi.replay(), at the tick period the
  handler returns, rounded to whole output samples as the synthesiser does; pitch ratio as func_8007E36C computes it (ratios of 2 or
  more are replaced by 1, as the game does); amplitude sqrt(volume) x player
  volume / 16; an equal-power pan (pan 0 comes out on the right).
- stops: a key-on, instrument change or sample offset on a busy voice ramps
  it to silence over 256 samples and stops it at 320; from then on the
  voice's updates and starts land 320 samples late (the libultra voice
  offset the game sets to 0x140).
- loops run forward from loop start to loop end; one-shots end at their
  length. Resampling is linear (the game's RSP uses a 4-tap filter).
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dcm_to_midi as dcm  # noqa: E402

SAMPLE_BANK_ROM = 0x1DC6C0
OUTPUT_RATE_VADDR = 0x800E28E0    # the synthesiser's output rate (u32)
RATE = 44100
STOP_RAMP = 256                    # samples: the 0xB (set volume 0) update
STOP_AT = 320                      # samples: the 0xF (stop voice) update
VOICE_OFFSET = 320                 # pvoice offset after the first stop
CHANGE_RAMP = 32                   # samples, smoothing for volume and pan steps
PLAYER_VOLUME = {"jingle": 0x7FFF, "music": 0x4FFF}


# --------------------------------------------------------------------------
# Samples


def unpack_s(src: bytes, size: int) -> bytes:
    return dcm.lz_decompress(src, size)


def load_sample(rom: dcm.Rom, inst: dcm.Instrument) -> np.ndarray:
    """Decoded sample as float32 in [-1, 1), at the precision the game keeps."""
    data = rom.data
    count = struct.unpack(">H", data[SAMPLE_BANK_ROM:SAMPLE_BANK_ROM + 2])[0]
    if not 0 <= inst.sample_id < count:
        raise SystemExit(f"sample id {inst.sample_id:#x} outside the bank")
    at = SAMPLE_BANK_ROM + 2 + 4 * inst.sample_id
    start, end = struct.unpack(">II", data[at:at + 8])
    entry = data[SAMPLE_BANK_ROM + start:SAMPLE_BANK_ROM + end]
    passes = entry[0]
    letters = entry[1:1 + passes].decode("latin-1")
    length = struct.unpack(">I", entry[1 + passes:5 + passes])[0]
    body = entry[5 + passes:5 + passes + length]
    if inst.flags & 1:
        if letters:
            raise SystemExit(f"sample {inst.sample_id:#x}: unexpected passes {letters!r}")
        pcm = np.frombuffer(body[:inst.length * 2], dtype="<i2").astype(np.int32)
        if not inst.looped:
            pcm = (pcm >> 8) << 8          # stored as 8-bit by func_8007A088
    else:
        if letters != "SD":
            raise SystemExit(f"sample {inst.sample_id:#x}: unsupported passes {letters!r}")
        raw = np.frombuffer(unpack_s(body, inst.length), dtype=np.uint8)
        # pass D: out[0] = in[0]; out[i] = out[i-1] - in[i] (mod 256)
        signs = np.concatenate(([raw[0].astype(np.int64)], -raw[1:].astype(np.int64)))
        pcm = (np.cumsum(signs) & 0xFF).astype(np.uint8).view(np.int8).astype(np.int32) << 8
    out = np.zeros(inst.length, dtype=np.float32)
    out[:len(pcm)] = pcm[:inst.length] / 32768.0
    return out


# --------------------------------------------------------------------------
# Rendering


class Note:
    __slots__ = ("voice", "inst", "start", "offset", "controls", "end", "fade")

    def __init__(self, voice, inst, start, offset):
        self.voice = voice
        self.inst = inst
        self.start = start            # output sample
        self.offset = offset          # start frame in the sample
        self.controls = []            # (sample, ratio, gain_l, gain_r)
        self.end = None               # output sample where it stops
        self.fade = None              # sample where the stop ramp starts


def pitch_ratio(pm: dcm.PitchModel, word: int, out_rate: int) -> float:
    value = pm.value(word)
    if value <= 0:
        return 0.0
    period = pm.clock // value
    if period == 0:
        return 0.0
    ratio = pm.ref_period / period * pm.ref_rate / out_rate
    return 1.0 if ratio >= 2.0 else ratio


def pan_gains(pan: int) -> tuple[float, float]:
    """(left, right) for the player's pan byte >> 1. Equal power; 0 is the
    right side in the game's output (matched against a capture)."""
    angle = max(0, min(127, pan)) / 127 * math.pi / 2
    return math.sin(angle), math.cos(angle)


def collect_notes(song: dcm.Song, pm: dcm.PitchModel, out_rate: int,
                  player_volume: int):
    """Replay the song and return its notes and its length in samples."""
    nv = song.voices
    st = [dict(inst=None, word=0, vol=0, pan=64, offset=0, note=None, latency=0)
          for _ in range(nv)]
    notes = []
    clock = 0                          # output samples

    def gains(s):
        amp = math.sqrt(s["vol"]) * player_volume / 16 / 32767
        left, right = pan_gains(s["pan"])
        return amp * left, amp * right

    def control(s, at):
        n = s["note"]
        if n is not None:
            gl, gr = gains(s)
            n.controls.append((at, pitch_ratio(pm, s["word"], out_rate), gl, gr))

    def stop(s, at):
        n = s["note"]
        if n is not None:
            n.fade = at
            n.end = at + STOP_AT
            s["note"] = None
            s["latency"] = VOICE_OFFSET

    for tick, tick_us, events in dcm.replay(song):
        now = clock
        by_voice = {}
        for e in events:
            by_voice.setdefault(e.voice, []).append(e)
        for v, evs in by_voice.items():
            s = st[v]
            keyed = any(e.kind == "key" for e in evs)
            changed = False
            for e in evs:
                if e.kind == "pitch":
                    s["word"] = e.value
                    changed = True
                elif e.kind == "vol":
                    s["vol"] = e.value
                    changed = True
                elif e.kind == "pan":
                    s["pan"] = e.value
                    changed = True
                elif e.kind == "inst":
                    s["inst"] = e.value
                    stop(s, now)
                elif e.kind == "offset":
                    s["offset"] = e.value
                    stop(s, now)
            if keyed:
                stop(s, now)
                if s["inst"] is not None and s["inst"] < len(song.instruments):
                    inst = song.instruments[s["inst"]]
                    frames = s["offset"] // (2 if inst.looped else 1)
                    n = Note(v, s["inst"], now + s["latency"], frames)
                    s["note"] = n
                    notes.append(n)
                    control(s, n.start)
                s["offset"] = 0
            elif changed:
                control(s, now + s["latency"])
        # the synthesiser turns the handler's microseconds into whole samples
        # (libultra's _timeToSamplesNoRound: x * rate / 1e6 + 0.5, truncated)
        clock += int(tick_us * out_rate / 1e6 + 0.5)
    total = clock
    for s in st:
        if s["note"] is not None:
            s["note"].end = total + s["latency"]
    return notes, total


def render_note(note: Note, inst: dcm.Instrument, pcm: np.ndarray):
    """Return (start sample, stereo float32 block) for one note."""
    start, end = note.start, note.end
    if end <= start or not note.controls:
        return start, np.zeros((0, 2), np.float32)
    n = end - start
    idx = np.arange(n)
    times = np.array([c[0] - start for c in note.controls], dtype=np.int64)
    ratios = np.array([c[1] for c in note.controls])
    # piecewise-constant pitch, applied from each control's time
    seg = np.searchsorted(times, idx, side="right") - 1
    seg = np.clip(seg, 0, len(times) - 1)
    step = ratios[seg]
    pos = note.offset + np.concatenate(([0.0], np.cumsum(step[:-1])))
    if inst.looped and inst.loop_end > inst.loop_start:
        ls, le = inst.loop_start, min(inst.loop_end, len(pcm))
        over = pos >= le
        pos[over] = ls + np.mod(pos[over] - ls, le - ls)
        valid = n
    else:
        past = np.nonzero(pos >= len(pcm) - 1)[0]
        valid = int(past[0]) if len(past) else n
    pos = pos[:valid]
    i0 = pos.astype(np.int64)
    frac = (pos - i0).astype(np.float32)
    i1 = i0 + 1
    if inst.looped and inst.loop_end > inst.loop_start:
        le = min(inst.loop_end, len(pcm))
        i1 = np.where(i1 >= le, inst.loop_start, i1)
    i1 = np.minimum(i1, len(pcm) - 1)
    wave_ = pcm[i0] * (1 - frac) + pcm[i1] * frac
    # gains: ramp briefly to each new value
    gx, gl, gr = [0.0], [note.controls[0][2]], [note.controls[0][3]]
    for (t, _, l, r) in note.controls[1:]:
        t -= start
        gx += [t, t + CHANGE_RAMP]
        gl += [gl[-1], l]
        gr += [gr[-1], r]
    if note.fade is not None:
        f = max(0, note.fade - start)
        keep = [k for k, x in enumerate(gx) if x <= f]
        fl = float(np.interp(f, [gx[k] for k in keep], [gl[k] for k in keep]))
        fr = float(np.interp(f, [gx[k] for k in keep], [gr[k] for k in keep]))
        gx = [gx[k] for k in keep] + [f, f + STOP_RAMP]
        gl = [gl[k] for k in keep] + [fl, 0.0]
        gr = [gr[k] for k in keep] + [fr, 0.0]
    gx = np.maximum.accumulate(np.array(gx, dtype=np.float64))
    t = np.arange(valid)
    left = np.interp(t, gx, gl).astype(np.float32)
    right = np.interp(t, gx, gr).astype(np.float32)
    block = np.empty((valid, 2), np.float32)
    block[:, 0] = wave_ * left
    block[:, 1] = wave_ * right
    return start, block


def write_wav(path: Path, data: np.ndarray, scale: float):
    pcm = np.clip(np.round(data * scale * 32767), -32768, 32767).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def render_song(rom, pm, index, out_dir: Path, voices: bool, stems: bool,
                effects: set[int] | None, player: str, normalize: bool):
    song = dcm.parse_song(index, rom.song(index))
    out_rate = struct.unpack(">I", rom.vaddr(OUTPUT_RATE_VADDR, 4))[0]
    notes, total = collect_notes(song, pm, out_rate, PLAYER_VOLUME[player])
    pcms = {i: load_sample(rom, ins) for i, ins in enumerate(song.instruments)}
    length = max([total] + [n.end for n in notes]) + 1
    mix = np.zeros((length, 2), np.float32)
    per_voice = [np.zeros((length, 2), np.float32) for _ in range(song.voices)] if voices else None
    if effects is None:
        effects = {ins.sample_id for ins in song.instruments if not ins.looped}
    groups = {"effects": np.zeros((length, 2), np.float32),
              "music": np.zeros((length, 2), np.float32)} if stems else None
    for note in notes:
        inst = song.instruments[note.inst]
        start, block = render_note(note, inst, pcms[note.inst])
        if not len(block):
            continue
        sl = slice(start, start + len(block))
        mix[sl] += block
        if per_voice is not None:
            per_voice[note.voice][sl] += block
        if groups is not None:
            groups["effects" if inst.sample_id in effects else "music"][sl] += block
    peak = float(np.max(np.abs(mix))) or 1.0
    scale = 0.98 / peak if normalize else min(1.0, 0.98 / peak)
    write_wav(out_dir / f"song_{index}_mix.wav", mix, scale)
    written = [out_dir / f"song_{index}_mix.wav"]
    if per_voice is not None:
        for v, buf in enumerate(per_voice):
            p = out_dir / f"song_{index}_ch{v:02d}.wav"
            write_wav(p, buf, scale)
            written.append(p)
    if groups is not None:
        for name, buf in groups.items():
            p = out_dir / f"song_{index}_{name}.wav"
            write_wav(p, buf, scale)
            written.append(p)
    used = sorted({song.instruments[n.inst].sample_id for n in notes})
    print(f"song {index}: {song.voices} voices, {len(notes)} notes, "
          f"{length / RATE:.2f} s at {RATE} Hz (synth rate {out_rate}), "
          f"peak {peak:.3f}, scale {scale:.3f}")
    if stems:
        print("  effects samples:", " ".join(f"{s:#x}" for s in used if s in effects))
        print("  music samples:  ", " ".join(f"{s:#x}" for s in used if s not in effects))
    for p in written:
        print("  wrote", p)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--song", type=int, action="append", default=[],
                    help="song table index (repeatable)")
    ap.add_argument("--out-dir", type=Path, default=Path(".local/music"))
    ap.add_argument("--voices", action="store_true", help="also write one WAV per voice")
    ap.add_argument("--stems", action="store_true", help="also write effects and music stems")
    ap.add_argument("--effects-samples",
                    help="comma-separated sample ids (hex or decimal) for the effects stem; "
                         "default: every one-shot (unlooped) instrument's sample")
    ap.add_argument("--player", choices=sorted(PLAYER_VOLUME), default=None,
                    help="player volume: jingle (0x7FFF, the intro's player) or music "
                         "(0x4FFF, the background music at the default balance); "
                         "default: music for song 0, jingle otherwise")
    ap.add_argument("--normalize", action="store_true",
                    help="scale the mix to full range (default: the game's level, "
                         "scaled down only if it would clip)")
    args = ap.parse_args(argv)
    if not args.song:
        ap.error("give at least one --song")
    effects = None
    if args.effects_samples:
        effects = {int(x, 0) for x in args.effects_samples.split(",") if x.strip()}
    rom = dcm.Rom(args.rom.read_bytes())
    pm = dcm.PitchModel.from_rom(rom)
    for index in args.song:
        player = args.player or ("music" if index == 0 else "jingle")
        render_song(rom, pm, index, args.out_dir, args.voices, args.stems,
                    effects, player, args.normalize)
    return 0


if __name__ == "__main__":
    sys.exit(main())
