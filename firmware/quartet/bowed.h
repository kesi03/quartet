/* QUARTET -- one bowed-string engine, four chairs.
 *
 * A compact, clean-room C adaptation of the McIntyre / Schumacher / Woodhouse
 * bowed-string waveguide (the model used by STK's Bowed instrument): two
 * fractional delay lines either side of the bow, a nonlinear friction table,
 * a lossy string filter, body resonators, bow rosin and finger vibrato.
 *
 * The engine is audio-flow only: fixed static arrays, no heap, no libc maths,
 * no host calls.  All control values arrive once per block in qt_ctrl.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef QT_BOWED_H
#define QT_BOWED_H

#include "vibe.h"

#define QT_CHAIRS          4
#define QT_VOICES_PER_CHAIR 4
#define QT_VOICES          (QT_CHAIRS * QT_VOICES_PER_CHAIR)
#define QT_STRINGS         3   /* concert unison: 1..3 strings per voice */
#define QT_DLINE           4096 /* fractional samples, covers 23 Hz at 96 kHz */
#define QT_RV_COMBS        4
#define QT_CH_MAX          1024 /* chorus: 10.7 ms at 96 kHz */
#define QT_DL_MAX         60000 /* delay: 0.62 s at 96 kHz */
#define QT_EC_MAX        122880 /* echo: two taps up to 1.28 s at 96 kHz */

/* Articulations: the 14-style minimum set.  Order matches the UI menu. */
enum {
  QT_STYLE_SUST_VIB = 0, QT_STYLE_SUST_PLAIN, QT_STYLE_LEGATO,
  QT_STYLE_PORTAMENTO, QT_STYLE_STACCATO, QT_STYLE_SPICCATO,
  QT_STYLE_TREMOLO, QT_STYLE_PIZZICATO, QT_STYLE_HARMONIC,
  QT_STYLE_TRILL, QT_STYLE_SUL_TASTO, QT_STYLE_SUL_PONT,
  QT_STYLE_MARCATO, QT_STYLE_DOUBLE_STOP, QT_STYLE_COUNT
};

typedef struct {
  const char *name;
  float pressure;             /* 0..1, sets the friction-table slope */
  float beta;                 /* bow-to-bridge fraction of the string */
  float bow;                  /* base bow velocity */
  float loop_pole;            /* one-pole string loss filter */
  float body_f[2];            /* two body formants, Hz */
  float body_r[2];            /* pole radii of those formants */
  float gain;                 /* preset trim */
  int   strings;              /* 1 solo .. 3 section */
  float detune;               /* cents between unison strings */
  int   octave;               /* octave displacement of the preset */
} qt_preset;

typedef struct {
  float volume;
  float pressure;             /* master bow pressure, 0..1 */
  float position;             /* bow position trim, 0..1 */
  float speed;                /* master bow speed, 0..1 */
  float rosin;                /* rosin noise, 0..1 */
  float vib_rate;             /* Hz */
  float vib_depth;            /* 0..1 */
  float body;                 /* body resonator mix, 0..1 */
  float bridge;               /* bridge formant emphasis, 0..1 */
  float room;                 /* reverb send, 0..1 */
  float rv_decay;             /* reverb feedback, 0..1 */
  float rv_damp;              /* reverb HF damping, 0..1 */
  float chorus_mix;           /* stereo chorus crossfade, 0..1 */
  float chorus_rate;          /* chorus LFO rate, Hz */
  float chorus_depth;         /* chorus delay depth, 0..1 */
  float delay_mix;            /* clean stereo delay crossfade, 0..1 */
  float delay_time;           /* delay time, seconds */
  float delay_fb;             /* delay feedback, 0..1 */
  float echo_mix;             /* multi-tap echo crossfade, 0..1 */
  float echo_time;            /* echo tap spacing, seconds */
  float echo_fb;              /* echo feedback, 0..1 */
  float detune;               /* extra unison detune, cents */
  float unison;               /* 0 = preset value, else 1..3 strings */
  float attack;               /* seconds */
  float release;              /* seconds */
  float level[QT_CHAIRS];     /* per-chair mixing level */
  int   preset[QT_CHAIRS];    /* preset index for each chair */
  int   style[QT_CHAIRS];     /* articulation index for each chair */
} qt_ctrl;

void  qt_bowed_init(float sample_rate);
void  qt_bowed_ctrl(const qt_ctrl *c);
void  qt_bowed_note_on(int source_note, int note, int vel, int chair);
void  qt_bowed_note_off(int source_note);
void  qt_bowed_all_off(void);
void  qt_bowed_render(float *out_l, float *out_r, int n);
int   qt_bowed_voices(void);
int   qt_bowed_strings(void);
const qt_preset *qt_preset_at(int i);

#define QT_PRESET_COUNT 8

#endif
