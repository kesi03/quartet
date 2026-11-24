/* QUARTET -- see bowed.h.  A compact C adaptation of the bowed-string
 * waveguide published by McIntyre, Schumacher and Woodhouse and popularised by
 * Julius Smith / the STK Bowed class.  Two fractional delay lines either side
 * of the bow, a nonlinear friction characteristic, a lossy string loop filter,
 * bandpass body formants, bow rosin and finger vibrato.  Clean room, no samples,
 * no impulse responses, no heap, no libc maths and no host calls in audio.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bowed.h"
#include "vibe_dsp.h"

/* ---- presets ---------------------------------------------------------------- */
static const qt_preset PRESETS[QT_PRESET_COUNT] = {
  /* name             press beta  bow   loop   body f0/f1        radii       gain str det oct */
  { "SOLO VIOLIN",    0.82f, 0.12f, 0.070f, 0.9960f, { 2750.f, 5200.f }, { 0.93f, 0.88f }, 0.82f, 1, 0.0f,  0 },
  { "SECTION VIOLIN", 0.78f, 0.13f, 0.062f, 0.9950f, { 2650.f, 4900.f }, { 0.92f, 0.87f }, 0.56f, 3, 6.0f,  0 },
  { "SOLO VIOLA",     0.84f, 0.13f, 0.064f, 0.9965f, { 1650.f, 3600.f }, { 0.93f, 0.88f }, 0.84f, 1, 0.0f,  0 },
  { "SECTION VIOLA",  0.80f, 0.14f, 0.058f, 0.9955f, { 1600.f, 3450.f }, { 0.92f, 0.87f }, 0.58f, 3, 5.0f,  0 },
  { "SOLO CELLO",     0.86f, 0.12f, 0.058f, 0.9970f, {  950.f, 2300.f }, { 0.94f, 0.89f }, 0.88f, 1, 0.0f,  0 },
  { "SECTION CELLO",  0.82f, 0.13f, 0.052f, 0.9960f, {  900.f, 2200.f }, { 0.93f, 0.88f }, 0.60f, 2, 4.0f,  0 },
  { "DOUBLE BASS",    0.84f, 0.11f, 0.044f, 0.9975f, {  520.f, 1250.f }, { 0.95f, 0.90f }, 0.92f, 1, 0.0f, -1 },
  { "OCTOBASS",       0.86f, 0.10f, 0.038f, 0.9980f, {  330.f,  820.f }, { 0.96f, 0.91f }, 0.96f, 1, 0.0f, -2 }
};

const qt_preset *qt_preset_at(int i)
{
  if (i < 0) i = 0;
  if (i >= QT_PRESET_COUNT) i = QT_PRESET_COUNT - 1;
  return &PRESETS[i];
}

/* ---- fractional delay ------------------------------------------------------- */
typedef struct {
  float buf[QT_DLINE];
  int   pos;
  float len;
  float last;
} qt_delay;

static void dly_init(qt_delay *d)
{
  int i;
  for (i = 0; i < QT_DLINE; i++) d->buf[i] = 0.0f;
  d->pos = 0;
  d->len = 16.0f;
  d->last = 0.0f;
}

static float dly_tick(qt_delay *d, float x)
{
  float q = (float)d->pos - d->len;
  int i, j;
  float f, y;
  while (q < 0.0f) q += (float)QT_DLINE;
  while (q >= (float)QT_DLINE) q -= (float)QT_DLINE;
  i = (int)q;
  f = q - (float)i;
  j = i + 1;
  if (j >= QT_DLINE) j = 0;
  y = d->buf[i] + (d->buf[j] - d->buf[i]) * f;
  d->buf[d->pos] = x;
  d->pos++;
  if (d->pos >= QT_DLINE) d->pos = 0;
  d->last = y;
  return y;
}

/* ---- one string ------------------------------------------------------------- */
typedef struct {
  qt_delay neck, bridge;
  float    low;       /* string loop loss filter state */
  float    freq;
  float    phase;     /* vibrato phase */
  float    rosin;     /* rosin noise lowpass */
  float    detune;    /* cents */
} qt_string;

/* ---- body / bridge formant state -------------------------------------------- */
typedef struct {
  float b0, b2, a1, a2, z1, z2;
} qt_biquad;

/* ---- one voice -------------------------------------------------------------- */
typedef struct {
  int    active;
  int    releasing;
  int    strings;
  int    source;
  int    note;
  int    chair;
  int    preset;
  int    style;
  int    octave;
  unsigned int age;
  float  held;
  float  auto_off;
  float  glide_from, glide_to, glide_t, glide_dur;
  float  trill_t, trill_other;
  int    trill_high;
  float  trem_phase;
  float  pluck;
  float  env;
  float  vel;
  float  gain;
  float  pan;
  float  attack, release;
  float  body_mix;
  float  bridge_mix;
  float  pressure;
  float  beta;
  float  bow;
  float  loop_pole;
  float  vib_rate, vib_depth;
  float  rosin_amt;
  qt_biquad body1, body2, bridge;
  qt_string s[QT_STRINGS];
} qt_voice;

/* ---- body / bridge formants (RBJ bandpass biquad) ---------------------------- *
 * A Chamberlin SVF is only stable while its F coefficient stays below its Q
 * coefficient.  Body formants are 330 Hz .. 5.2 kHz at Q 6..18, so that limit
 * is broken at the top of the range and the filter howls.  Use a normalised
 * biquad instead: coefficients are set once per note, states stay bounded.    */
static void bp_set(qt_biquad *bq, float freq, float resonance, float sr)
{
  float ph = freq / sr;
  float sn, cs, alpha, a0;
  if (ph < 0.002f) ph = 0.002f;
  if (ph > 0.45f) ph = 0.45f;
  if (resonance < 0.5f) resonance = 0.5f;
  sn = vd_sin(ph);
  cs = vd_sin(ph + 0.25f);
  alpha = sn * 0.5f / resonance;
  a0 = 1.0f + alpha;
  bq->b0 = alpha / a0;
  bq->b2 = -alpha / a0;
  bq->a1 = -2.0f * cs / a0;
  bq->a2 = (1.0f - alpha) / a0;
  bq->z1 = 0.0f;
  bq->z2 = 0.0f;
}

static float bp_tick(qt_biquad *bq, float x)
{
  float y = bq->b0 * x + bq->z1;
  bq->z1 = bq->b2 * x - bq->a1 * y + bq->z2;
  bq->z2 = bq->b2 * x - bq->a2 * y;
  return y;
}

/* ---- stereo reverb ---------------------------------------------------------- */
#define QT_RV_MAX_COMB 4600
#define QT_RV_MAX_AP    700

typedef struct { float buf[QT_RV_MAX_COMB]; float lp; int pos, size; } qt_comb;
typedef struct { float buf[QT_RV_MAX_AP]; int pos, size; } qt_ap;
typedef struct { qt_comb c[QT_RV_COMBS]; qt_ap a[2]; } qt_reverb;

static const float RV_COMB_TIME[QT_RV_COMBS] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
static const float RV_AP_TIME[2] = { 0.0050f, 0.0070f };
static qt_reverb REV;
static float RV_DAMP;
static float RV_FEEDBACK;
static float RV_WET;
static float RV_COEF;
static unsigned int NOISE = 0x1234abcdu;
static float SR;

static void reverb_init(float sr)
{
  int i;
  for (i = 0; i < QT_RV_COMBS; i++) {
    int size = (int)(RV_COMB_TIME[i] * sr);
    if (size < 16) size = 16;
    if (size >= QT_RV_MAX_COMB) size = QT_RV_MAX_COMB - 1;
    REV.c[i].size = size;
  }
  for (i = 0; i < 2; i++) {
    int size = (int)(RV_AP_TIME[i] * sr);
    if (size < 8) size = 8;
    if (size >= QT_RV_MAX_AP) size = QT_RV_MAX_AP - 1;
    REV.a[i].size = size;
  }
  for (i = 0; i < QT_RV_COMBS; i++) {
    int k;
    for (k = 0; k < QT_RV_MAX_COMB; k++) REV.c[i].buf[k] = 0.0f;
    REV.c[i].lp = 0.0f;
    REV.c[i].pos = 0;
  }
  for (i = 0; i < 2; i++) {
    int k;
    for (k = 0; k < QT_RV_MAX_AP; k++) REV.a[i].buf[k] = 0.0f;
    REV.a[i].pos = 0;
  }
  RV_DAMP = 0.24f;
  RV_FEEDBACK = 0.78f;
  RV_WET = 0.0f;
  RV_COEF = vd_coef(0.02f, sr);
}

/* Comb / allpass state is deliberately shared between the two channels with a
 * channel offset in the read pointer: a tiny stereo spread at no extra cost. */
static float comb_tick(qt_comb *c, float x, int off)
{
  float y, q = (float)(c->pos - c->size + off);
  int i;
  while (q < 0.0f) q += (float)QT_RV_MAX_COMB;
  i = (int)q;
  y = c->buf[i];
  c->lp = y * (1.0f - RV_DAMP) + c->lp * RV_DAMP;
  c->buf[c->pos] = x + c->lp * RV_FEEDBACK;
  c->pos++;
  if (c->pos >= QT_RV_MAX_COMB) c->pos = 0;
  return y;
}

static float ap_tick(qt_ap *a, float x, int off)
{
  float q = (float)(a->pos - a->size + off), y, z;
  int i;
  while (q < 0.0f) q += (float)QT_RV_MAX_AP;
  i = (int)q;
  y = a->buf[i];
  z = x - y * 0.5f;
  a->buf[a->pos] = z;
  a->pos++;
  if (a->pos >= QT_RV_MAX_AP) a->pos = 0;
  return y + z * 0.5f;
}

/* ---- chorus, delay and echo -------------------------------------------------- *
 * All three are fixed-memory sends fed from the dry quartet mix.  Keeping their
 * feedback states separate means the controls cannot make the string waveguide
 * unstable, and every buffer is sized for the highest supported sample rate.  */
typedef struct { float buf[QT_CH_MAX]; int pos; float phase; } qt_ch;
typedef struct { float buf[QT_DL_MAX]; float lp; int pos; } qt_dl;
typedef struct { float buf[QT_EC_MAX]; float lp; int pos; } qt_ec;

static qt_ch CH[2];
static qt_dl DL[2];
static qt_ec EC[2];
static float CH_MIX, CH_RATE, CH_DEPTH;
static float DL_MIX, DL_FB, DL_LEN;
static float EC_MIX, EC_FB, EC_LEN;

static float fx_clamp(float x, float lo, float hi)
{
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static float line_read(const float *buf, int size, int pos, float len)
{
  float q = (float)pos - len;
  int i, j;
  float f;
  while (q < 0.0f) q += (float)size;
  i = (int)q;
  f = q - (float)i;
  j = i + 1;
  if (j >= size) j = 0;
  return buf[i] + (buf[j] - buf[i]) * f;
}

static float chorus_tick(qt_ch *c, float x, float dt)
{
  float len = 0.004f * SR + CH_DEPTH * 0.003f * SR * vd_sin(c->phase);
  float y;
  if (len < 2.0f) len = 2.0f;
  if (len > (float)QT_CH_MAX - 2.0f) len = (float)QT_CH_MAX - 2.0f;
  y = line_read(c->buf, QT_CH_MAX, c->pos, len);
  c->buf[c->pos] = x;
  c->pos++;
  if (c->pos >= QT_CH_MAX) c->pos = 0;
  c->phase += CH_RATE * dt;
  if (c->phase >= 1.0f) c->phase -= 1.0f;
  return y;
}

static float delay_tick(qt_dl *d, float x, float len)
{
  float y, fb;
  if (len < 4.0f) len = 4.0f;
  if (len > (float)QT_DL_MAX - 2.0f) len = (float)QT_DL_MAX - 2.0f;
  y = line_read(d->buf, QT_DL_MAX, d->pos, len);
  fb = d->lp + (y - d->lp) * 0.38f;
  d->lp = fb;
  d->buf[d->pos] = x + fb * DL_FB;
  d->pos++;
  if (d->pos >= QT_DL_MAX) d->pos = 0;
  return y;
}

static float echo_tick(qt_ec *e, float x, float len)
{
  float t1, t2, y, fb;
  if (len < 8.0f) len = 8.0f;
  if (len > (float)QT_EC_MAX * 0.5f - 2.0f) len = (float)QT_EC_MAX * 0.5f - 2.0f;
  t1 = line_read(e->buf, QT_EC_MAX, e->pos, len);
  t2 = line_read(e->buf, QT_EC_MAX, e->pos, len * 2.0f);
  y = t1 * 0.68f + t2 * 0.32f;
  fb = e->lp + (t2 - e->lp) * 0.24f;
  e->lp = fb;
  e->buf[e->pos] = x + fb * EC_FB;
  e->pos++;
  if (e->pos >= QT_EC_MAX) e->pos = 0;
  return y;
}

static void effects_clear(void)
{
  int i, k;
  for (i = 0; i < 2; i++) {
    for (k = 0; k < QT_CH_MAX; k++) CH[i].buf[k] = 0.0f;
    CH[i].pos = 0;
    CH[i].phase = i ? 0.37f : 0.0f;
    for (k = 0; k < QT_DL_MAX; k++) DL[i].buf[k] = 0.0f;
    DL[i].pos = 0;
    DL[i].lp = 0.0f;
    for (k = 0; k < QT_EC_MAX; k++) EC[i].buf[k] = 0.0f;
    EC[i].pos = 0;
    EC[i].lp = 0.0f;
  }
}

static void effects_init(void)
{
  effects_clear();
  CH_MIX = 0.0f; CH_RATE = 0.35f; CH_DEPTH = 0.25f;
  DL_MIX = 0.0f; DL_FB = 0.0f; DL_LEN = 0.24f * SR;
  EC_MIX = 0.0f; EC_FB = 0.0f; EC_LEN = 0.17f * SR;
}

/* ---- engine state ----------------------------------------------------------- */
static qt_voice VOICES[QT_VOICES];
static qt_ctrl  CTRL;
static float    MASTER;
static unsigned int AGE;
static int      ACTIVE_CHAIR[QT_CHAIRS];
static int      LAST_NOTE[QT_CHAIRS];
static int      LAST_SET[QT_CHAIRS];
static int      ACTIVE_STRINGS;
static float    DC_X_L, DC_X_R, DC_Y_L, DC_Y_R, DC_R;

static float noise(void)
{
  NOISE ^= NOISE << 13;
  NOISE ^= NOISE >> 17;
  NOISE ^= NOISE << 5;
  return ((float)(NOISE & 0xffffu) / 32768.0f) - 1.0f;
}

void qt_bowed_init(float sample_rate)
{
  int v, i;
  SR = sample_rate > 8000.0f ? sample_rate : 48000.0f;
  for (v = 0; v < QT_VOICES; v++) {
    qt_voice *o = &VOICES[v];
    o->active = 0; o->releasing = 0; o->source = -1; o->note = 60; o->chair = v & 3;
    o->preset = v & 3; o->age = 0; o->env = 0.0f; o->vel = 0.0f;
    o->gain = 0.0f; o->pan = 0.0f;
    for (i = 0; i < QT_STRINGS; i++) {
      dly_init(&o->s[i].neck);
      dly_init(&o->s[i].bridge);
      o->s[i].low = 0.0f;
      o->s[i].freq = 440.0f;
      o->s[i].phase = 0.0f;
      o->s[i].rosin = 0.0f;
      o->s[i].detune = 0.0f;
    }
  }
  for (i = 0; i < QT_CHAIRS; i++) ACTIVE_CHAIR[i] = 0;
  ACTIVE_STRINGS = 0;
  AGE = 0;
  MASTER = 1.0f;
  DC_R = 1.0f - 80.0f / SR;   /* ~13 Hz high-pass at 48 kHz, keeps bow DC out */
  if (DC_R < 0.99f) DC_R = 0.99f;
  if (DC_R > 0.9999f) DC_R = 0.9999f;
  DC_X_L = 0.0f; DC_X_R = 0.0f; DC_Y_L = 0.0f; DC_Y_R = 0.0f;
  reverb_init(SR);
  effects_init();
  CTRL.volume = 0.8f;
  CTRL.pressure = 0.5f;
  CTRL.position = 0.5f;
  CTRL.speed = 0.5f;
  CTRL.rosin = 0.35f;
  CTRL.vib_rate = 5.2f;
  CTRL.vib_depth = 0.25f;
  CTRL.body = 0.65f;
  CTRL.bridge = 0.5f;
  CTRL.room = 0.35f;
  CTRL.rv_decay = 0.6f;
  CTRL.rv_damp = 0.35f;
  CTRL.chorus_mix = 0.0f;
  CTRL.chorus_rate = 0.35f;
  CTRL.chorus_depth = 0.25f;
  CTRL.delay_mix = 0.0f;
  CTRL.delay_time = 0.24f;
  CTRL.delay_fb = 0.3f;
  CTRL.echo_mix = 0.0f;
  CTRL.echo_time = 0.17f;
  CTRL.echo_fb = 0.28f;
  CTRL.detune = 0.0f;
  CTRL.unison = 0.0f;
  CTRL.attack = 0.035f;
  CTRL.release = 0.28f;
  for (i = 0; i < QT_CHAIRS; i++) {
    CTRL.level[i] = 0.85f;
    CTRL.style[i] = QT_STYLE_SUST_VIB;
    LAST_NOTE[i] = 60;
    LAST_SET[i] = 0;
  }
  CTRL.preset[0] = 1; CTRL.preset[1] = 3; CTRL.preset[2] = 5; CTRL.preset[3] = 6;
}

void qt_bowed_ctrl(const qt_ctrl *c)
{
  int i;
  if (!c) return;
  CTRL = *c;
  MASTER = CTRL.volume;
  RV_WET = fx_clamp(CTRL.room, 0.0f, 1.0f) * 0.45f;
  RV_FEEDBACK = 0.52f + 0.40f * fx_clamp(CTRL.rv_decay, 0.0f, 1.0f);
  RV_DAMP = 0.10f + 0.78f * fx_clamp(CTRL.rv_damp, 0.0f, 1.0f);

  CH_MIX = fx_clamp(CTRL.chorus_mix, 0.0f, 1.0f) * 0.60f;
  CH_RATE = fx_clamp(CTRL.chorus_rate, 0.05f, 2.0f);
  CH_DEPTH = fx_clamp(CTRL.chorus_depth, 0.0f, 1.0f);

  DL_MIX = fx_clamp(CTRL.delay_mix, 0.0f, 1.0f) * 0.50f;
  DL_FB = fx_clamp(CTRL.delay_fb, 0.0f, 1.0f) * 0.72f;
  DL_LEN = fx_clamp(CTRL.delay_time, 0.02f, 0.60f) * SR;

  EC_MIX = fx_clamp(CTRL.echo_mix, 0.0f, 1.0f) * 0.40f;
  EC_FB = fx_clamp(CTRL.echo_fb, 0.0f, 1.0f) * 0.68f;
  EC_LEN = fx_clamp(CTRL.echo_time, 0.04f, 0.60f) * SR;

  for (i = 0; i < QT_VOICES; i++) {
    qt_voice *o = &VOICES[i];
    const qt_preset *p = qt_preset_at(o->preset);
    float reson = 8.0f + 10.0f * CTRL.body;
    bp_set(&o->body1, p->body_f[0], reson, SR);
    bp_set(&o->body2, p->body_f[1], reson * 0.8f, SR);
    bp_set(&o->bridge, p->body_f[1] * 0.65f, 6.4f, SR);
  }
}

static void update_chair_counts(void)
{
  int i, c, n = 0;
  for (c = 0; c < QT_CHAIRS; c++) ACTIVE_CHAIR[c] = 0;
  for (i = 0; i < QT_VOICES; i++) {
    qt_voice *o = &VOICES[i];
    if (!o->active) continue;
    ACTIVE_CHAIR[o->chair]++;
    if (o->env > 0.001f) n += o->strings;
  }
  ACTIVE_STRINGS = n;
}

void qt_bowed_note_on(int source_note, int note, int vel, int chair)
{
  int i, pick = -1, oldest = -1;
  unsigned int age = 0;
  if (chair < 0) chair = 0;
  if (chair >= QT_CHAIRS) chair = QT_CHAIRS - 1;
  if (note < 12) note = 12;
  if (note > 108) note = 108;
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;

  /* Replace an existing voice from the same source and chair first (retrigger). */
  for (i = 0; i < QT_VOICES; i++) {
    qt_voice *o = &VOICES[i];
    if (o->active && o->source == source_note && o->chair == chair) { pick = i; break; }
  }
  if (pick < 0) {
    for (i = 0; i < QT_VOICES; i++) {
      qt_voice *o = &VOICES[i];
      if (!o->active && o->chair == chair) { pick = i; break; }
    }
  }
  if (pick < 0) {
    for (i = 0; i < QT_VOICES; i++) {
      qt_voice *o = &VOICES[i];
      if (!o->active) { pick = i; break; }
    }
  }
  if (pick < 0) {
    for (i = 0; i < QT_VOICES; i++) {
      if (VOICES[i].age >= age) { age = VOICES[i].age; oldest = i; }
    }
    pick = oldest;
  }
  if (pick < 0) pick = 0;

  {
    qt_voice *o = &VOICES[pick];
    const qt_preset *p = qt_preset_at(CTRL.preset[chair]);
    int strings = p->strings;
    int s, style = CTRL.style[chair];
    int play_note = note;
    float det = p->detune + CTRL.detune;
    if (style < 0 || style >= QT_STYLE_COUNT) style = QT_STYLE_SUST_VIB;
    if (style == QT_STYLE_HARMONIC) {
      play_note += 12;
      if (play_note > 127) play_note = 127;
    }
    if (CTRL.unison >= 1.0f) strings = (int)CTRL.unison;
    if (strings < 1) strings = 1;
    if (strings > QT_STRINGS) strings = QT_STRINGS;

    o->active = 1;
    o->releasing = 0;
    o->strings = strings;
    o->source = source_note;
    o->note = play_note;
    o->chair = chair;
    o->preset = CTRL.preset[chair];
    o->style = style;
    o->octave = p->octave;
    o->age = ++AGE;
    o->held = 0.0f;
    o->auto_off = 0.0f;
    o->glide_from = (float)play_note;
    o->glide_to = (float)play_note;
    o->glide_t = 0.0f;
    o->glide_dur = 0.0f;
    o->trill_t = 0.0f;
    o->trill_high = 0;
    o->trill_other = (float)(play_note + 2 < 127 ? play_note + 2 : 127);
    o->trem_phase = 0.0f;
    o->pluck = 0.0f;
    o->env = 0.0f;
    if (style == QT_STYLE_PORTAMENTO && LAST_SET[chair] && LAST_NOTE[chair] != play_note) {
      o->glide_from = (float)LAST_NOTE[chair];
      o->glide_to = (float)play_note;
      o->glide_dur = 0.24f;
    }
    LAST_NOTE[chair] = play_note;
    LAST_SET[chair] = 1;

    o->vel = (float)vel / 127.0f;
    o->gain = p->gain * CTRL.level[chair] * (0.55f + 0.45f * o->vel);
    o->pan = (chair == 0 ? -0.55f : (chair == 1 ? -0.18f : (chair == 2 ? 0.18f : 0.55f)));
    o->attack = CTRL.attack;
    o->release = CTRL.release;
    o->body_mix = CTRL.body * 0.75f;
    o->bridge_mix = CTRL.bridge;
    o->pressure = p->pressure;
    o->beta = p->beta + (CTRL.position - 0.5f) * 0.06f;
    o->bow = p->bow * (0.60f + 0.50f * CTRL.speed);
    o->loop_pole = p->loop_pole;
    o->vib_rate = CTRL.vib_rate;
    o->vib_depth = CTRL.vib_depth;
    o->rosin_amt = CTRL.rosin;

    switch (style) {
    case QT_STYLE_SUST_VIB:
      o->vib_depth = CTRL.vib_depth * 1.35f;
      break;
    case QT_STYLE_SUST_PLAIN:
      o->vib_depth = 0.0f;
      break;
    case QT_STYLE_LEGATO:
      o->attack = CTRL.attack * 1.6f;
      o->release = CTRL.release * 1.4f;
      o->vib_depth = CTRL.vib_depth * 0.8f;
      o->bow *= 0.92f;
      break;
    case QT_STYLE_PORTAMENTO:
      o->vib_depth = CTRL.vib_depth * 0.9f;
      o->bow *= 0.96f;
      break;
    case QT_STYLE_STACCATO:
      o->attack = 0.006f;
      o->release = 0.05f;
      o->auto_off = 0.13f;
      o->vib_depth = 0.0f;
      o->bow *= 1.18f;
      break;
    case QT_STYLE_SPICCATO:
      o->attack = 0.003f;
      o->release = 0.06f;
      o->auto_off = 0.11f;
      o->vib_depth = 0.0f;
      o->bow *= 1.05f;
      o->rosin_amt = 0.35f + 0.45f * CTRL.rosin;
      break;
    case QT_STYLE_TREMOLO:
      o->vib_depth = CTRL.vib_depth * 0.3f;
      o->bow *= 0.9f;
      break;
    case QT_STYLE_PIZZICATO:
      o->attack = 0.001f;
      o->release = 0.10f;
      o->auto_off = 0.30f;
      o->pluck = 1.0f;
      o->vib_depth = 0.0f;
      o->bow *= 1.6f;
      o->body_mix *= 0.85f;
      o->bridge_mix *= 0.75f;
      break;
    case QT_STYLE_HARMONIC:
      o->vib_depth = CTRL.vib_depth * 0.5f;
      o->bow *= 0.62f;
      o->body_mix *= 0.45f;
      o->bridge_mix *= 0.35f;
      break;
    case QT_STYLE_TRILL:
      o->vib_depth = CTRL.vib_depth * 0.4f;
      o->bow *= 0.95f;
      break;
    case QT_STYLE_SUL_TASTO:
      o->beta *= 0.45f;
      o->body_mix *= 0.85f;
      o->bridge_mix *= 0.45f;
      o->vib_depth = CTRL.vib_depth * 0.6f;
      o->bow *= 0.9f;
      break;
    case QT_STYLE_SUL_PONT:
      o->beta = o->beta * 2.0f + 0.07f;
      o->body_mix *= 0.7f;
      o->bridge_mix = o->bridge_mix * 1.5f + 0.2f;
      o->vib_depth = CTRL.vib_depth * 0.3f;
      o->bow *= 1.05f;
      break;
    case QT_STYLE_MARCATO:
      o->attack = 0.004f;
      o->release = 0.16f;
      o->pressure = 0.95f;
      o->vib_depth = CTRL.vib_depth * 0.7f;
      o->bow *= 1.45f;
      break;
    case QT_STYLE_DOUBLE_STOP:
      o->vib_depth = CTRL.vib_depth * 0.7f;
      o->bow *= 0.9f;
      break;
    default:
      break;
    }
    if (o->beta < 0.06f) o->beta = 0.06f;
    if (o->beta > 0.32f) o->beta = 0.32f;
    if (o->vib_depth < 0.0f) o->vib_depth = 0.0f;
    if (o->vib_depth > 1.2f) o->vib_depth = 1.2f;
    if (o->bridge_mix > 1.2f) o->bridge_mix = 1.2f;
    bp_set(&o->body1, p->body_f[0], 8.0f + 10.0f * CTRL.body, SR);
    bp_set(&o->body2, p->body_f[1], 6.4f + 8.0f * CTRL.body, SR);
    bp_set(&o->bridge, p->body_f[1] * 0.65f, 6.4f, SR);

    for (s = 0; s < QT_STRINGS; s++) {
      qt_string *st = &o->s[s];
      float f = vd_mtof((float)(o->note + o->octave * 12));
      if (s < strings) {
        st->detune = (s == 0 ? -det : (s == 1 ? 0.0f : det)) * 0.5f;
        st->freq = f * vd_pow(2.0f, st->detune / 1200.0f);
        if (st->freq < 20.0f) st->freq = 20.0f;
        if (st->freq > SR * 0.4f) st->freq = SR * 0.4f;
      } else {
        st->freq = f;
      }
      st->phase = (float)s * 0.27f;
      st->rosin = 0.0f;
      st->low = 0.0f;
      dly_init(&st->neck);
      dly_init(&st->bridge);
      st->neck.len = (SR / st->freq - 4.0f) * (1.0f - o->beta);
      st->bridge.len = (SR / st->freq - 4.0f) * o->beta;
    }
  }
  ACTIVE_STRINGS = 0;
  update_chair_counts();
}

void qt_bowed_note_off(int source_note)
{
  int i;
  for (i = 0; i < QT_VOICES; i++)
    if (VOICES[i].active && VOICES[i].source == source_note)
      VOICES[i].releasing = 1;
  update_chair_counts();
}

void qt_bowed_all_off(void)
{
  int i;
  for (i = 0; i < QT_VOICES; i++) {
    VOICES[i].active = 0;
    VOICES[i].releasing = 0;
    VOICES[i].source = -1;
    VOICES[i].env = 0.0f;
  }
  for (i = 0; i < QT_CHAIRS; i++) {
    ACTIVE_CHAIR[i] = 0;
    LAST_SET[i] = 0;
  }
  ACTIVE_STRINGS = 0;
  effects_clear();
}

int qt_bowed_voices(void)
{
  int n = 0, i;
  for (i = 0; i < QT_VOICES; i++) if (VOICES[i].active) n++;
  return n;
}

int qt_bowed_strings(void)
{
  return ACTIVE_STRINGS;
}

/* friction table: (|dv*slope + 0.001| + 0.75)^-4, clamped 0.01..0.98 */
static float bow_table(float dv, float slope)
{
  float a = dv * slope + 0.001f;
  float r;
  if (a < 0.0f) a = -a;
  a += 0.75f;
  r = 1.0f / a;
  r *= r;
  r *= r;
  if (r < 0.01f) r = 0.01f;
  if (r > 0.98f) r = 0.98f;
  return r;
}

void qt_bowed_render(float *out_l, float *out_r, int n)
{
  const float dt = 1.0f / SR;
  int frame, v;

  for (frame = 0; frame < n; frame++) {
    float mix_l = 0.0f, mix_r = 0.0f;
    for (v = 0; v < QT_VOICES; v++) {
      qt_voice *o = &VOICES[v];
      float raw = 0.0f, bridge_out, body_out, sample, env, trem = 1.0f;
      int s;
      if (!o->active) continue;
      if (o->releasing) {
        o->env -= dt / (o->release < 0.02f ? 0.02f : o->release);
        if (o->env <= 0.0f) {
          o->env = 0.0f; o->active = 0; o->releasing = 0;
          continue;
        }
      } else {
        o->env += dt / (o->attack < 0.002f ? 0.002f : o->attack);
        if (o->env > 1.0f) o->env = 1.0f;
      }
      env = o->env;
      o->held += dt;
      if (o->auto_off > 0.0f && !o->releasing && o->held >= o->auto_off)
        o->releasing = 1;
      if (o->style == QT_STYLE_PIZZICATO) o->pluck *= 0.9990f;

      {
        float cur_note = (float)o->note;
        if (o->style == QT_STYLE_TRILL) {
          o->trill_t += dt;
          if (o->trill_t >= 0.11f) {
            o->trill_t -= 0.11f;
            o->trill_high = o->trill_high ? 0 : 1;
          }
          cur_note = o->trill_high ? o->trill_other : (float)o->note;
        }
        if (o->style == QT_STYLE_PORTAMENTO && o->glide_t < o->glide_dur) {
          float t = o->glide_t / o->glide_dur;
          t = t * t * (3.0f - 2.0f * t);
          cur_note = o->glide_from + (o->glide_to - o->glide_from) * t;
          o->glide_t += dt;
          if (o->glide_t > o->glide_dur) o->glide_t = o->glide_dur;
        }
        if (o->style == QT_STYLE_TREMOLO) {
          o->trem_phase += 11.0f * dt;
          if (o->trem_phase >= 1.0f) o->trem_phase -= 1.0f;
          trem = 0.62f + 0.38f * vd_sin(o->trem_phase);
        }
        if (o->style == QT_STYLE_TRILL || o->style == QT_STYLE_PORTAMENTO) {
          for (s = 0; s < o->strings; s++) {
            qt_string *st = &o->s[s];
            float f = vd_mtof(cur_note + (float)o->octave * 12.0f);
            f *= vd_pow(2.0f, st->detune / 1200.0f);
            st->freq = f;
            if (st->freq < 20.0f) st->freq = 20.0f;
            if (st->freq > SR * 0.4f) st->freq = SR * 0.4f;
          }
        }
      }

      for (s = 0; s < o->strings; s++) {
        qt_string *st = &o->s[s];
        float base = SR / st->freq - 4.0f;
        float vib = 0.012f * o->vib_depth * vd_sin(st->phase);
        float bow_v = (o->style == QT_STYLE_PIZZICATO)
                    ? o->bow * o->pluck * 1.4f : o->bow * env;
        float bridge_ref, nut_ref, string_v, dv, nv, lp, filtered;
        float slope = 2.0f - 0.60f * o->pressure;
        st->phase += o->vib_rate * dt;
        if (st->phase >= 1.0f) st->phase -= 1.0f;
        st->neck.len = base * (1.0f - o->beta) * (1.0f - vib);
        st->bridge.len = base * o->beta * (1.0f - vib);
        if (st->neck.len < 2.0f) st->neck.len = 2.0f;
        if (st->neck.len > (float)QT_DLINE - 2.0f) st->neck.len = (float)QT_DLINE - 2.0f;
        if (st->bridge.len < 2.0f) st->bridge.len = 2.0f;
        if (st->bridge.len > (float)QT_DLINE - 2.0f) st->bridge.len = (float)QT_DLINE - 2.0f;

        /* Bow velocity has a little rosin-modulated irregularity. */
        if (o->rosin_amt > 0.001f) {
          st->rosin = st->rosin * 0.88f + noise() * 0.12f;
          bow_v *= 1.0f + st->rosin * o->rosin_amt * 0.16f;
        }

        /* STK-style lossy string filter: a damped one-pole in the bridge
         * reflection.  The pole must stay well below 1 or the loop howls. */
        lp = st->low + (st->bridge.last - st->low) * 0.34f;
        st->low = lp;
        filtered = lp * 0.95f;
        bridge_ref = -filtered;
        nut_ref = -st->neck.last;
        string_v = bridge_ref + nut_ref;
        dv = bow_v - string_v;
        nv = (o->style == QT_STYLE_PIZZICATO)
           ? bow_v * (0.75f + 0.25f * noise())
           : dv * bow_table(dv, slope);
        dly_tick(&st->neck, bridge_ref + nv);
        dly_tick(&st->bridge, nut_ref + nv);
        raw += st->bridge.last;
        if (o->rosin_amt > 0.001f)
          raw += st->rosin * o->rosin_amt * 0.0035f * env;
      }
      raw /= (float)o->strings;

      /* Bridge formant and two body formants, shared by the voice's unisons. */
      bridge_out = bp_tick(&o->bridge, raw);
      body_out = bp_tick(&o->body1, raw);
      body_out += bp_tick(&o->body2, raw) * 0.6f;
      sample = raw * (1.0f - o->body_mix)
             + body_out * o->body_mix * 0.55f
             + bridge_out * o->bridge_mix * 0.30f;
      sample *= o->gain * env * 0.55f * trem;
      mix_l += sample * (0.5f - 0.5f * o->pan);
      mix_r += sample * (0.5f + 0.5f * o->pan);
    }

    mix_l *= MASTER * 12.0f;
    mix_r *= MASTER * 12.0f;

    /* Chorus, delay and echo are fixed-memory sends from the dry quartet mix.
     * Ticking them even while muted keeps their buffers fresh on enable. */
    {
      float dry_l = mix_l, dry_r = mix_r;
      float cl = chorus_tick(&CH[0], dry_l, dt);
      float cr = chorus_tick(&CH[1], dry_r, dt);
      float dl = delay_tick(&DL[0], dry_l, DL_LEN);
      float dr = delay_tick(&DL[1], dry_r, DL_LEN);
      float el = echo_tick(&EC[0], dry_l, EC_LEN);
      float er = echo_tick(&EC[1], dry_r, EC_LEN);

      mix_l += (cl * 0.82f + cr * 0.18f - dry_l) * CH_MIX;
      mix_r += (cr * 0.82f + cl * 0.18f - dry_r) * CH_MIX;
      mix_l += dl * DL_MIX;
      mix_r += dr * DL_MIX;
      mix_l += el * EC_MIX;
      mix_r += er * EC_MIX;
    }

    if (RV_WET > 0.001f) {
      float wl = 0.0f, wr = 0.0f, yl, yr, send = RV_WET * 0.22f;
      int i;
      for (i = 0; i < QT_RV_COMBS; i++) {
        wl += comb_tick(&REV.c[i], mix_l * send, 0);
        wr += comb_tick(&REV.c[i], mix_r * send, 17);
      }
      yl = wl;
      yr = wr;
      for (i = 0; i < 2; i++) {
        yl = ap_tick(&REV.a[i], yl, 0);
        yr = ap_tick(&REV.a[i], yr, 11);
      }
      mix_l += yl * 0.75f;
      mix_r += yr * 0.75f;
    }
    /* Remove the bow's small DC offset before the last-resort soft clip. */
    {
      float hp_l = mix_l - DC_X_L + DC_R * DC_Y_L;
      float hp_r = mix_r - DC_X_R + DC_R * DC_Y_R;
      DC_X_L = mix_l; DC_X_R = mix_r;
      DC_Y_L = hp_l; DC_Y_R = hp_r;
      out_l[frame] = vd_tanh(hp_l * 0.72f);
      out_r[frame] = vd_tanh(hp_r * 0.72f);
    }
  }
  update_chair_counts();
}
