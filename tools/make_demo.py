#!/usr/bin/env python3
"""Generate QUARTET's on-device demo sequence (firmware/quartet/demo.h).

The demo is a four-part string arrangement of the riff section of the supplied
MIDI file in firmware/demo/.  Four lines are taken from that file and mapped
straight onto the quartet chairs:

    track 9   violin   the high riff (G#5 / B5 / A5 / F#5 / D6 / C#6)
    track 10  viola    the sustained G#4 / A4 inner line
    track 6   cello    the low E3 / B2 / D3 / A2 pulse
    track 3   bass     the A1 / E2 / B1 line (raised an octave for the
                       DOUBLE BASS preset, which transposes down an octave)

Bars 28-35 of the source are an eight-bar stretch where all four lines are
present.  Tempo, pitches and note lengths come from the MIDI file itself; only
the velocities are rebalanced (riff up, backing down) so the tune stays audible
over the three accompanying lines.  The generated header is pure C constant
data and needs no MIDI reader on the device.

Run from the repository root:  python tools/make_demo.py
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware" / "demo" / "THE VERVE.Bitter sweet simphony K.mid"
OUT = ROOT / "firmware" / "quartet" / "demo.h"

PPQ = 192                       # ticks per quarter note in the source file
START_TICK = 27 * 4 * PPQ       # bar 28
END_TICK = START_TICK + 8 * 4 * PPQ   # eight bars later (bar 36)

# (MIDI track, chair, transpose semitones, pitch filter)
LINES = [
    (9, 0, 0, (78, 86)),   # violin riff
    (10, 1, 0, (68, 69)),  # viola inner line
    (6, 2, 0, (45, 52)),   # cello pulse
    (3, 3, 12, (33, 40)),  # bass, up an octave for the bass preset
]


def read_midi(path):
    """Small standard MIDI-file reader: enough for the note data we need."""
    data = path.read_bytes()
    pos = 0

    def be(n):
        nonlocal pos
        v = int.from_bytes(data[pos:pos + n], "big")
        pos += n
        return v

    def vlq():
        nonlocal pos
        v = 0
        while True:
            c = data[pos]
            pos += 1
            v = (v << 7) | (c & 0x7f)
            if not c & 0x80:
                return v

    assert data[:4] == b"MThd", "not a MIDI file"
    pos = 4
    hlen = be(4)
    fmt, ntracks, division = be(2), be(2), be(2)
    assert division > 0 and not (division & 0x8000), "need delta-time PPQ"
    pos += hlen - 6

    tracks = []
    tempo_us = None
    for _ in range(ntracks):
        assert data[pos:pos + 4] == b"MTrk", "bad track header at %d: %r" % (pos, data[pos:pos + 8])
        pos += 4
        track_len = be(4)
        end = pos + track_len
        t = 0
        status = None
        on = {}
        notes = []
        while pos < end:
            t += vlq()
            c = data[pos]
            if c & 0x80:
                status = c
                pos += 1
            if status == 0xFF:
                kind = data[pos]
                pos += 1
                size = vlq()
                payload = data[pos:pos + size]
                pos += size
                if kind == 0x51 and len(payload) == 3 and (tempo_us is None or t == 0):
                    tempo_us = int.from_bytes(payload, "big")
            elif status in (0xF0, 0xF7):
                pos += vlq()
            else:
                hi = status & 0xF0
                if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    d1, d2 = data[pos], data[pos + 1]
                    pos += 2
                    if hi == 0x90 and d2 > 0:
                        on[d1] = (t, d2)
                    elif hi == 0x80 or (hi == 0x90 and d2 == 0):
                        if d1 in on:
                            start, vel = on.pop(d1)
                            notes.append((start, t - start, d1, vel))
                elif hi in (0xC0, 0xD0):
                    pos += 1
                else:
                    raise ValueError("unsupported MIDI status %02x" % status)
        for d1, (start, vel) in on.items():
            notes.append((start, end - start, d1, vel))
        notes.sort()
        tracks.append(notes)
        pos = end
    return tracks, division, (tempo_us if tempo_us is not None else 500000)


def build():
    tracks, ppq, tempo_us = read_midi(SOURCE)
    sec_per_tick = (tempo_us / 1_000_000.0) / ppq
    events = []
    ident = 1000

    for track, chair, transpose, (lo, hi) in LINES:
        if track >= len(tracks):
            continue
        for start, length, pitch, vel in tracks[track]:
            if not (lo <= pitch <= hi):
                continue
            if start < START_TICK or start >= END_TICK:
                continue
            dur = min(length, END_TICK - start) * sec_per_tick
            if dur < 0.06:
                dur = 0.06
            if dur > 2.0:
                dur = 2.0
            note = pitch + transpose
            if note < 24 or note > 100:
                continue
            t = (start - START_TICK) * sec_per_tick
            # Keep the riff in front without changing a single note or its timing.
            balance = 1.15 if chair == 0 else 0.62
            vel = max(1, min(127, int(vel * balance + 0.5)))
            events.append((t, dur, note, vel, ident, chair))
            ident += 1

    events.sort(key=lambda e: (e[0], e[5], e[2]))

    lines = [
        "/* Generated by tools/make_demo.py from firmware/demo/ -- do not edit. */",
        "#ifndef QT_DEMO_H",
        "#define QT_DEMO_H",
        "",
        "typedef struct { float t, dur; int note, vel, id, chair; } qt_demo_event;",
        "",
        "static const qt_demo_event QT_DEMO[] = {",
    ]
    for t, dur, note, vel, ident, chair in events:
        lines.append("  { %.4ff, %.4ff, %d, %d, %d, %d }," %
                     (t, dur, note, vel, ident, chair))
    lines.append("};")
    lines.append("#define QT_DEMO_N %d" % len(events))
    lines.append("#define QT_DEMO_LEN %.4ff" %
                 ((END_TICK - START_TICK) * sec_per_tick))
    lines.append("")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def main():
    OUT.write_text(build(), encoding="utf-8")
    print("wrote %s (%d events)" % (OUT, len(OUT.read_text(encoding="utf-8").splitlines())))


if __name__ == "__main__":
    main()
