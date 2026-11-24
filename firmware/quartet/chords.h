/* QUARTET -- original octatonic / polychordal quartet chords for the pads.
 *
 * Each entry is ordered high to low: violin, viola, cello, bass.  The voicings
 * are deliberately bitey — added seconds, tritone poles and stacked sevenths —
 * in the spirit of early-20th-century Russian chamber music, but written here
 * from scratch.  No existing piece is quoted.
 *
 * Generated shape: four notes, all within MIDI 44..82.
 */
#ifndef QT_CHORDS_H
#define QT_CHORDS_H

typedef struct { const char *name; int note[QT_CHAIRS]; } qt_chord;

static const qt_chord QT_CHORDS[16] = {
  { "BITONAL C",      { 70, 67, 61, 60 } },
  { "DIM SEVENTH",    { 71, 68, 65, 62 } },
  { "G7 SHARP NINE",  { 68, 65, 62, 55 } },
  { "ALTERED A",      { 67, 63, 60, 57 } },
  { "ELEVENTH C",     { 64, 58, 55, 48 } },
  { "POLY D",         { 63, 59, 53, 50 } },
  { "OCTATONIC E",    { 65, 59, 56, 52 } },
  { "B TRITONE",      { 62, 56, 53, 47 } },
  { "F SPLIT",        { 66, 60, 54, 53 } },
  { "C SHARP BITE",   { 64, 58, 55, 49 } },
  { "BB / C SHARP",   { 61, 55, 52, 46 } },
  { "AB POLYCHORD",   { 59, 53, 50, 44 } },
  { "E / F CLUSTER",  { 74, 71, 65, 64 } },
  { "D DIM STACK",    { 77, 71, 68, 62 } },
  { "B / C SEMITONE", { 72, 66, 60, 59 } },
  { "C DIM POLY",     { 69, 66, 63, 60 } }
};

#define QT_CHORD_COUNT 16

#endif
