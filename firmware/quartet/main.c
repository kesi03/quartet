/* QUARTET -- a physical-modelling string quartet for VIBE FM.
 *
 * Four slots (violin, viola, cello, bass), sixteen bowed-string voices and one
 * shared McIntyre / Schumacher / Woodhouse waveguide engine.  Keys and MIDI
 * play only the selected slot; pads and the demo still sound all four.  The FX
 * chain adds reverb controls, stereo chorus, clean delay and two-tap echo.
 * F3/PLAY runs a four-part arrangement of the MIDI riff line supplied in
 * firmware/demo/.
 * No samples, no impulse responses, no heap and no libc maths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"
#include "bowed.h"
#include "chords.h"
#include "demo.h"

VIBE_PARALLEL_AUDIO

/* ---- parameters -------------------------------------------------------------- */
enum {
  P_VOLUME = 0, P_MODE, P_SELECT, P_OCTAVE, P_TRANSPOSE,
  P_PRESSURE, P_POSITION, P_SPEED, P_ROSIN,
  P_VIB_RATE, P_VIB_DEPTH,
  P_BODY, P_BRIDGE, P_ROOM, P_DETUNE, P_UNISON,
  P_ATTACK, P_RELEASE,
  P_PRESET0, P_PRESET1, P_PRESET2, P_PRESET3,
  P_LEVEL0, P_LEVEL1, P_LEVEL2, P_LEVEL3,
  P_MUTE0, P_MUTE1, P_MUTE2, P_MUTE3,
  P_DEMO_LOOP,
  /* Appended: pads can also fire original Stravinsky-style quartet chords. */
  P_PAD_MODE, P_CHORD_DUR, P_CHORD_VEL,
  /* Appended: spatial and time-based effects. */
  P_RV_DECAY, P_RV_DAMP,
  P_CHORUS_MIX, P_CHORUS_RATE, P_CHORUS_DEPTH,
  P_DELAY_MIX, P_DELAY_TIME, P_DELAY_FB,
  P_ECHO_MIX, P_ECHO_TIME, P_ECHO_FB,
  /* Appended: one articulation style per slot. */
  P_STYLE0, P_STYLE1, P_STYLE2, P_STYLE3,
  P_COUNT
};

static const char *const PRESET_NAMES[QT_PRESET_COUNT] = {
  "SOLO VIOLIN", "SECTION VIOLIN", "SOLO VIOLA", "SECTION VIOLA",
  "SOLO CELLO", "SECTION CELLO", "DOUBLE BASS", "OCTOBASS"
};
static const char *const CHAIR_NAMES[QT_CHAIRS] = { "VIOLIN", "VIOLA", "CELLO", "BASS" };
static const char *const ON_OFF[] = { "OFF", "ON" };
static const char *const PAD_MODES[] = { "CHORDS", "CTRL" };
static const char *const KEY_MODES[] = { "SELECT" };
static const char *const OCTS[] = { "-2", "-1", "0", "+1", "+2" };
static const char *const STYLE_NAMES[] = {
  "SUST VIB", "SUST PLAIN", "LEGATO", "PORTAMENTO",
  "STACCATO", "SPICCATO", "TREMOLO", "PIZZICATO",
  "HARMONIC", "TRILL", "SUL TASTO", "SUL PONT",
  "MARCATO", "DBL STOP"
};

#define PV(id,n,mi,ma,de,u) { id, n, mi, ma, de, u, 0, 0 }
#define PE(id,n,mi,ma,de,u,st,ch) { id, n, mi, ma, de, u, st, ch }

static const vibe_param PARAMS[P_COUNT] = {
  PV("volume",    "Volume",      0, 1.2f, 0.82f, ""),
  PE("routing",   "Keyboard",    0, 0, 0, "", 1, KEY_MODES),
  PE("chair",     "Chair",       0, 3, 0, "", 4, CHAIR_NAMES),
  PE("octave",    "Octave",      0, 4, 2, "", 5, OCTS),
  PV("transpose", "Transpose", -12, 12, 0, "st"),

  PV("pressure",  "Bow Pressure", 0, 1, 0.70f, ""),
  PV("position",  "Bow Position", 0, 1, 0.50f, ""),
  PV("speed",     "Bow Speed",    0, 1, 0.50f, ""),
  PV("rosin",     "Rosin",        0, 1, 0.35f, ""),

  PV("vib_rate",  "Vibrato Rate", 2, 9, 5.2f, "Hz"),
  PV("vib_depth", "Vibrato Depth", 0, 1, 0.26f, ""),

  PV("body",      "Body",        0, 1, 0.65f, ""),
  PV("bridge",    "Bridge",      0, 1, 0.50f, ""),
  PV("room",      "Room",        0, 1, 0.34f, ""),
  PV("detune",    "Unison Detune", 0, 25, 5, "ct"),
  PV("unison",    "Unison",      0, 3, 0, "str"),

  PV("attack",    "Attack",     0.005f, 0.40f, 0.045f, "s"),
  PV("release",   "Release",    0.03f, 1.20f, 0.32f, "s"),

  PE("preset_violin", "Violin Preset", 0, 7, 1, "", 8, PRESET_NAMES),
  PE("preset_viola",  "Viola Preset",  0, 7, 3, "", 8, PRESET_NAMES),
  PE("preset_cello",  "Cello Preset",  0, 7, 5, "", 8, PRESET_NAMES),
  PE("preset_bass",   "Bass Preset",   0, 7, 6, "", 8, PRESET_NAMES),

  PV("level_violin", "Violin Level", 0, 1, 0.85f, ""),
  PV("level_viola",  "Viola Level",  0, 1, 0.82f, ""),
  PV("level_cello",  "Cello Level",  0, 1, 0.88f, ""),
  PV("level_bass",   "Bass Level",   0, 1, 0.90f, ""),

  PE("mute_violin", "Mute Violin", 0, 1, 0, "", 2, ON_OFF),
  PE("mute_viola",  "Mute Viola",  0, 1, 0, "", 2, ON_OFF),
  PE("mute_cello",  "Mute Cello",  0, 1, 0, "", 2, ON_OFF),
  PE("mute_bass",   "Mute Bass",   0, 1, 0, "", 2, ON_OFF),

  PE("demo_loop", "Demo Loop", 0, 1, 1, "", 2, ON_OFF),

  PE("pad_mode",    "Pads",        0, 1, 0, "", 2, PAD_MODES),
  PV("chord_dur",   "Chord Hold", 0.15f, 2.00f, 0.65f, "s"),
  PV("chord_vel",   "Chord Accent", 40, 127, 112, ""),

  PV("rv_decay",    "Verb Decay",  0, 1, 0.60f, ""),
  PV("rv_damp",     "Verb Damp",   0, 1, 0.35f, ""),
  PV("chorus_mix",  "Chorus",      0, 1, 0.00f, ""),
  PV("chorus_rate", "Chorus Rate", 0.05f, 2.00f, 0.35f, "Hz"),
  PV("chorus_depth","Chorus Depth",0, 1, 0.25f, ""),
  PV("delay_mix",   "Delay",       0, 1, 0.00f, ""),
  PV("delay_time",  "Delay Time",  0.02f, 0.60f, 0.24f, "s"),
  PV("delay_fb",    "Delay Fbk",   0, 1, 0.30f, ""),
  PV("echo_mix",    "Echo",        0, 1, 0.00f, ""),
  PV("echo_time",   "Echo Time",   0.04f, 0.60f, 0.17f, "s"),
  PV("echo_fb",     "Echo Fbk",    0, 1, 0.28f, ""),

  PE("style_violin", "Violin Style", 0, 13, 0, "", 14, STYLE_NAMES),
  PE("style_viola",  "Viola Style",  0, 13, 2, "", 14, STYLE_NAMES),
  PE("style_cello",  "Cello Style",  0, 13, 1, "", 14, STYLE_NAMES),
  PE("style_bass",   "Bass Style",   0, 13, 12, "", 14, STYLE_NAMES)
};
#undef PV
#undef PE

static const char *const GROUPS[P_COUNT] = {
  "SOUND · VOICE", "SOUND · VOICE", "SOUND · VOICE", "SOUND · VOICE", "SOUND · VOICE",
  "BOW · FRICTION", "BOW · FRICTION", "BOW · FRICTION", "BOW · FRICTION",
  "VIB · FINGER", "VIB · FINGER",
  "BODY · RESONANCE", "BODY · RESONANCE", "BODY · RESONANCE", "BODY · RESONANCE", "BODY · RESONANCE",
  "BOW · ENVELOPE", "BOW · ENVELOPE",
  "CHAIRS · PRESETS", "CHAIRS · PRESETS", "CHAIRS · PRESETS", "CHAIRS · PRESETS",
  "CHAIRS · LEVELS", "CHAIRS · LEVELS", "CHAIRS · LEVELS", "CHAIRS · LEVELS",
  "CHAIRS · MUTES", "CHAIRS · MUTES", "CHAIRS · MUTES", "CHAIRS · MUTES",
  "SYSTEM · DEMO",
  "CHORD · PADS", "CHORD · PADS", "CHORD · PADS",
  "FX · REVERB", "FX · REVERB",
  "FX · CHORUS", "FX · CHORUS", "FX · CHORUS",
  "FX · DELAY", "FX · DELAY", "FX · DELAY",
  "FX · ECHO", "FX · ECHO", "FX · ECHO",
  "STYLE · SLOT", "STYLE · SLOT", "STYLE · SLOT", "STYLE · SLOT"
};

static const vibe_card CARDS[] = {
  { "VOICE",     5, "Selected slot, octave, transposition, bow setup and master volume." },
  { "FRICTION",  4, "Bow pressure, position, speed and rosin grain." },
  { "ENVELOPE",  2, "Bow attack and release times." },
  { "FINGER",    2, "Humanised finger vibrato: rate and depth." },
  { "RESONANCE", 5, "Body formants, bridge emphasis, room and unison detune." },
  { "PRESETS",   4, "One preset per chair: solo and section strings, bass and octobass." },
  { "LEVELS",    4, "Per-chair mixer levels." },
  { "MUTES",     4, "Per-chair mutes; SHIFT + pads 9-12 are shortcuts." },
  { "PADS",      3, "Pads 1-16 fire original octatonic / polychordal quartet chords." },
  { "DEMO",      1, "Four-part riff demo from the bundled MIDI." },
  { "REVERB",    2, "Reverb decay and damping; the send is on BODY." },
  { "CHORUS",    3, "Stereo chorus with rate and delay depth." },
  { "DELAY",     3, "Clean stereo delay with time and feedback." },
  { "ECHO",      3, "Two-tap echo with tap spacing and feedback." },
  { "SLOT",      4, "One bowed-string articulation per slot." }
};

/* ---- pages ------------------------------------------------------------------ */
enum {
  UI_SOUND, UI_BOW, UI_BODY, UI_VIB, UI_CHORD, UI_STYLE,
  UI_CHAIRS, UI_LEVELS, UI_SPACE, UI_FX, UI_DELAY, UI_ECHO,
  UI_SYSTEM, UI_N
};
static const int PAGE_PARAM[UI_N][4] = {
  { P_SELECT, P_PRESSURE, P_SPEED, P_POSITION },
  { P_ROSIN, P_ATTACK, P_RELEASE, P_DETUNE },
  { P_BODY, P_BRIDGE, P_ROOM, P_UNISON },
  { P_VIB_RATE, P_VIB_DEPTH, P_ROSIN, P_PRESSURE },
  { P_PAD_MODE, P_CHORD_DUR, P_CHORD_VEL, P_SELECT },
  { P_STYLE0, P_STYLE1, P_STYLE2, P_STYLE3 },
  { P_SELECT, P_PRESET0, P_LEVEL0, P_MUTE0 },
  { P_LEVEL0, P_LEVEL1, P_LEVEL2, P_LEVEL3 },
  { P_ROOM, P_RV_DECAY, P_RV_DAMP, P_VOLUME },
  { P_CHORUS_MIX, P_CHORUS_RATE, P_CHORUS_DEPTH, P_ROOM },
  { P_DELAY_MIX, P_DELAY_TIME, P_DELAY_FB, P_CHORUS_MIX },
  { P_ECHO_MIX, P_ECHO_TIME, P_ECHO_FB, P_DELAY_MIX },
  { P_VOLUME, P_OCTAVE, P_TRANSPOSE, P_DEMO_LOOP }
};
static const char *const PAGE_NAME[UI_N] = {
  "SOUND", "BOW", "BODY", "VIB", "CHORD", "STYLE", "CHAIRS",
  "LEVELS", "SPACE", "FX", "DELAY", "ECHO", "SYSTEM"
};
static const int MORE_PAGES[] = {
  UI_CHAIRS, UI_LEVELS, UI_SPACE, UI_FX, UI_DELAY, UI_ECHO, UI_STYLE, UI_SYSTEM
};
static const char *const F_LABELS[6] = { "PAGE", "PRESET", "DEMO", "PREV", "HELP", "PANIC" };
static const char *const K_LABELS[6] = { "SOUND", "BOW", "BODY", "VIB/CH", "MORE", "FX" };

static const char *const HELP[] = {
  "KEYS / MIDI", "play only the selected slot, using that slot's style",
  "PADS 1-16", "fire original Stravinsky-style four-part chords",
  "SHIFT + PAD", "shortcuts: slots, presets, demo, mutes, slot select",
  "STYLE PAGE", "one of 14 bowed-string articulations per slot",
  "FX PAGES", "reverb decay/damp, chorus, delay and echo",
  "PAD 7 + SHIFT", "start or stop the four-part MIDI riff demo",
  "PAD 8 + SHIFT", "panic, all notes off",
  "F1 / ENC A", "next page",
  "F2 / ENC B", "next preset",
  "F4", "previous preset",
  "K1-K4", "SOUND / BOW / BODY / VIB+CHORD+STYLE",
  "K5", "cycle CHAIRS / LEVELS / SPACE / FX / DELAY / ECHO / STYLE / SYSTEM",
  "K6", "jump to the FX page",
  "KNOBS 1-4", "edit the four values shown; CHAIRS follows selection",
  "HOME / F5", "this overview",
  "DATA + ENTER", "FOCUS: hold ENTER, DATA walks and edits",
  "RETURN", "close the overview, back to SOUND"
};

/* ---- state ------------------------------------------------------------------ */
#define C_BG      0x100D16u
#define C_PANEL   0x211A2Cu
#define C_PANEL2  0x2E2540u
#define C_ACC     0xE8A33Bu
#define C_GOLD    0xFFD27Bu
#define C_CREAM   0xFFF0D8u
#define C_MUTED   0x9C93A8u
#define C_AVO     0x9FBF63u
#define C_VIOLET  0xBE9BFFu
#define C_BLUE    0x6BB6FFu
#define C_RED     0xFF6B6Bu

static float sr = 48000.0f;
static int g_params[P_COUNT];
static float ui_vals[P_COUNT];
static int ui_page, help, draw_tick, sel_cached;
static char status[512];
static int status_len, status_pending;
static volatile int meter_l, meter_r;
static int pad_flash[16];

static int aget(const volatile int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void aset(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static int scaled(float v) { return (int)(v * 1000.0f + (v >= 0 ? 0.5f : -0.5f)); }
static float q1000(int v) { return (float)v * 0.001f; }
/* Menu / preset / velocity parameters are stored as scaled q1000 integers. */
static int param_i(int id) { return (int)(q1000(aget(&g_params[id])) + 0.5f); }

static void set_param(int id, float v);

/* Control-flow -> audio-flow note queue.  type 1 = on, 2 = off, 3 = panic.
 * Each queued note carries its destination slot; a DOUBLE STOP posts two. */
#define QN 96
typedef struct { unsigned char type, note, vel; int source, chair; } qt_qnote;
static volatile qt_qnote Q[QN];
static volatile unsigned int qw, qr;
static volatile int last_note = -1;

static volatile int demo_cmd, demo_playing;
static int demo_index;
static float demo_playhead;
static int demo_active[64];
static int demo_active_n;
typedef struct { float t; int id; } qt_demo_off;
static qt_demo_off demo_off[64];
static int demo_off_n;

/* Pad chords: one timed voice group per pad hit.  The source ids are unique so
 * retriggering a pad never steals the previous chord's note-offs. */
typedef struct { int base, frames; } qt_chord_hold;
static qt_chord_hold CHORD_HOLD[16];
static int chord_hold_n;
static int chord_serial;
static int shift_down;

static void post(int type, int note, int vel, int source, int chair)
{
  unsigned int w, r;
  VIBE_ACQUIRE();
  r = qr;
  w = qw;
  if (w - r < QN) {
    Q[w % QN].type = (unsigned char)type;
    Q[w % QN].note = (unsigned char)(note & 127);
    Q[w % QN].vel = (unsigned char)(vel & 127);
    Q[w % QN].source = source;
    Q[w % QN].chair = chair;
    qw = w + 1;
  }
  VIBE_RELEASE();
  if (type == 1) last_note = note;
}

static void start_demo(void)
{
  demo_index = 0;
  demo_playhead = 0.0f;
  demo_active_n = 0;
  demo_off_n = 0;
  __atomic_store_n(&demo_cmd, 1, __ATOMIC_RELEASE);
}

static void stop_demo(void)
{
  __atomic_store_n(&demo_cmd, 2, __ATOMIC_RELEASE);
}

static void toggle_demo(void)
{
  if (__atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE)) stop_demo();
  else start_demo();
}

static void panic(void)
{
  int i;
  for (i = 0; i < 16; i++) pad_flash[i] = 0;
  chord_hold_n = 0;
  stop_demo();
  post(3, 0, 0, 0, -1);
}

/* ---- parameters and state ---------------------------------------------------- */
static void apply_param(int id, float v)
{
  if (id < 0 || id >= P_COUNT) return;
  if (v != v) v = PARAMS[id].def;
  if (v < PARAMS[id].min) v = PARAMS[id].min;
  if (v > PARAMS[id].max) v = PARAMS[id].max;
  if (PARAMS[id].steps > 1) {
    float span = PARAMS[id].max - PARAMS[id].min;
    int last = PARAMS[id].steps - 1;
    if (span > 0.0f && last > 0) {
      int k = (int)(0.5f + (v - PARAMS[id].min) * (float)last / span);
      if (k < 0) k = 0;
      if (k > last) k = last;
      v = PARAMS[id].min + span * (float)k / (float)last;
    }
  }
  ui_vals[id] = v;
  aset(&g_params[id], scaled(v));
  status_pending = 1;
}

static void set_param(int id, float v)
{
  apply_param(id, v);
  if (id >= 0 && id < P_COUNT) vibe_param_set(id, ui_vals[id]);
}

static void next_preset(int dir)
{
  int chair = (int)(ui_vals[P_SELECT] + 0.5f);
  int id = P_PRESET0 + chair;
  int n = (int)(ui_vals[id] + 0.5f) + dir;
  if (n < 0) n = QT_PRESET_COUNT - 1;
  if (n >= QT_PRESET_COUNT) n = 0;
  set_param(id, (float)n);
}

static void store_state(void)
{
  int32_t buf[P_COUNT], i;
  for (i = 0; i < P_COUNT; i++) buf[i] = (int32_t)aget(&g_params[i]);
  vibe_store_write("quartet-state", buf, (int)sizeof buf);
}

static void load_state(void)
{
  int32_t buf[P_COUNT];
  int i;
  float v;
  int n = vibe_store_read("quartet-state", buf, (int)sizeof buf);
  if (n != (int)sizeof buf) return;
  for (i = 0; i < P_COUNT; i++) {
    v = (float)buf[i] * 0.001f;
    /* Old firmware saved three routing modes; the keyboard is slot-only now. */
    if (i == P_MODE && v > 0.0f) v = 0.0f;
    if (v < PARAMS[i].min || v > PARAMS[i].max) return;
  }
  for (i = 0; i < P_COUNT; i++) {
    v = (float)buf[i] * 0.001f;
    if (i == P_MODE) v = 0.0f;
    set_param(i, v);
  }
}

/* ---- note routing ------------------------------------------------------------ */
static int auto_chair(int note)
{
  if (note >= 72) return 0;
  if (note >= 60) return 1;
  if (note >= 48) return 2;
  return 3;
}

static int chair_muted(int c)
{
  return aget(&g_params[P_MUTE0 + c]) > 500;
}

static void route_note(int note, int vel, int down)
{
  int c = (int)(ui_vals[P_SELECT] + 0.5f);
  int style;
  int src = note;
  if (c < 0) c = 0;
  if (c >= QT_CHAIRS) c = QT_CHAIRS - 1;
  style = param_i(P_STYLE0 + c);

  /* Both notes are always released; the second source is harmless if the
   * style was not DOUBLE STOP when the note was played. */
  if (!down) {
    int n2 = note + 7;
    if (n2 > 127) n2 = 127;
    post(2, note, 0, src, c);
    post(2, n2, 0, 100000 + note, c);
    return;
  }
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  if (chair_muted(c)) return;
  post(1, note, vel, src, c);
  if (style == QT_STYLE_DOUBLE_STOP) {
    int n2 = note + 7;
    if (n2 > 127) n2 = 127;
    post(1, n2, vel, 100000 + note, c);
  }
}

/* ---- buttons ----------------------------------------------------------------- */
static void set_page(int p)
{
  if (p < 0) p = 0;
  if (p >= UI_N) p = 0;
  ui_page = p;
  status_pending = 1;
}

static void handle_button(int id)
{
  int i;
  switch (id) {
    case VB_F1: set_page((ui_page + 1) % UI_N); break;
    case VB_F2: next_preset(1); break;
    case VB_F3: toggle_demo(); break;
    case VB_F4: next_preset(-1); break;
    case VB_F5:
    case VB_HOME: help = help ? 0 : 1; break;
    case VB_RETURN: help = 0; set_page(UI_SOUND); break;
    case VB_F6:
    case VB_REC: panic(); break;
    case VB_K1: set_page(UI_SOUND); break;
    case VB_K2: set_page(UI_BOW); break;
    case VB_K3: set_page(UI_BODY); break;
    case VB_K4:
      set_page(ui_page == UI_VIB ? UI_CHORD :
               (ui_page == UI_CHORD ? UI_STYLE : UI_VIB));
      break;
    case VB_K5: {
      int n = (int)(sizeof MORE_PAGES / sizeof MORE_PAGES[0]);
      int k = 0;
      for (i = 0; i < n; i++) if (MORE_PAGES[i] == ui_page) k = i;
      set_page(MORE_PAGES[(k + 1) % n]);
      break;
    }
    case VB_K6: set_page(UI_FX); break;
    case VB_OCT_DOWN:
      set_param(P_OCTAVE, ui_vals[P_OCTAVE] - 1.0f); break;
    case VB_OCT_UP:
      set_param(P_OCTAVE, ui_vals[P_OCTAVE] + 1.0f); break;
    case VB_PLAY: toggle_demo(); break;
    case VB_STOP: panic(); break;
    default: break;
  }
  if (id >= 0 && id < 16) pad_flash[id] = 6;
}

static void handle_pad(int pad, int down, float value)
{
  int vel;
  if (pad < 0 || pad >= 16) return;
  if (!down) return;
  pad_flash[pad] = 6;

  /* Default: every pad is a Stravinsky-style quartet chord.  SHIFT swaps back
   * to the chair / preset / transport shortcuts on any pad, whatever the mode. */
  if (shift_down || param_i(P_PAD_MODE) == 0) {
    vel = value > 0.01f ? (int)(value * 127.0f + 0.5f) : param_i(P_CHORD_VEL);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    post(4, pad, vel, ++chord_serial, -1);
    return;
  }

  if (pad < 4) {
    set_param(P_SELECT, (float)pad);
  } else if (pad == 4) {
    next_preset(-1);
  } else if (pad == 5) {
    next_preset(1);
  } else if (pad == 6) {
    toggle_demo();
  } else if (pad == 7) {
    panic();
  } else if (pad >= 8 && pad <= 11) {
    int id = P_MUTE0 + (pad - 8);
    set_param(id, ui_vals[id] > 0.5f ? 0.0f : 1.0f);
  } else if (pad >= 12 && pad < 15) {
    set_param(P_SELECT, (float)(pad - 12));
  } else if (pad == 15) {
    for (pad = 0; pad < QT_CHAIRS; pad++) set_param(P_MUTE0 + pad, 0.0f);
    pad = 15;
  }
}

/* ---- editor bridge ----------------------------------------------------------- */
static void handle_editor(const uint8_t *b, int n)
{
  if (!b || n < 1) return;
  if (b[0] == 0x20) { status_pending = 1; return; }
  if (b[0] == 0x21) { store_state(); return; }
  if (b[0] == 0x22) { panic(); return; }
  if (b[0] == 0x23) { toggle_demo(); return; }
  if (b[0] == 0x24) { next_preset(1); return; }
  if (b[0] == 0x25) { next_preset(-1); return; }
  if (b[0] == 0x26) { set_page((ui_page + 1) % UI_N); return; }
  if (b[0] == 0x27) {
    int c = (int)(ui_vals[P_SELECT] + 0.5f) + 1;
    if (c >= QT_CHAIRS) c = 0;
    set_param(P_SELECT, (float)c);
    return;
  }
  if (b[0] == 0x29) { set_page(UI_FX); return; }
  if (b[0] == 0x2a) { set_page(UI_STYLE); return; }
  if (b[0] == 0x28 && n >= 2) { post(4, b[1] & 15, param_i(P_CHORD_VEL), ++chord_serial, -1); return; }
  if (b[0] == 0x30 && n >= 2) {
    int id = b[1];
    if (id >= 0 && id < P_COUNT) {
      float v;
      if (n >= 6) {
        uint32_t bits = ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) |
                        ((uint32_t)b[4] << 8) | (uint32_t)b[5];
        v = (float)((int)bits) * 0.001f;
        set_param(id, v);
      }
    }
  }
}

static void build_status(void)
{
  int i, len = 0;
  char tmp[24];
  status[0] = 0;
  #define APP(s) do { const char *q_ = (s); while (*q_ && len < (int)sizeof(status) - 2) status[len++] = *q_++; } while (0)
  APP("{\"slot\":\"");
  APP(CHAIR_NAMES[param_i(P_SELECT)]);
  APP("\",\"style\":\"");
  APP(STYLE_NAMES[param_i(P_STYLE0 + param_i(P_SELECT))]);
  APP("\",\"page\":\"");
  APP(PAGE_NAME[ui_page]);
  APP("\",\"pads\":\"");
  APP(PAD_MODES[param_i(P_PAD_MODE)]);
  APP("\",\"voices\":");
  APP(vg_itoa(qt_bowed_voices(), tmp));
  APP(",\"strings\":");
  APP(vg_itoa(qt_bowed_strings(), tmp));
  APP(",\"demo\":");
  APP(__atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE) ? "1" : "0");
  APP(",\"presets\":[");
  for (i = 0; i < QT_CHAIRS; i++) {
    if (i) APP(",");
    APP("\"");
    APP(PRESET_NAMES[param_i(P_PRESET0 + i)]);
    APP("\"");
  }
  APP("],\"levels\":[");
  for (i = 0; i < QT_CHAIRS; i++) {
    if (i) APP(",");
    APP(vg_ftoa(q1000(aget(&g_params[P_LEVEL0 + i])), 2, tmp));
  }
  APP("],\"mutes\":[");
  for (i = 0; i < QT_CHAIRS; i++) {
    if (i) APP(",");
    APP(aget(&g_params[P_MUTE0 + i]) > 500 ? "1" : "0");
  }
  APP("]}");
  #undef APP
  status_len = len;
}

/* ---- events ------------------------------------------------------------------ */
void vibe_on_event(const vibe_event *e)
{
  int id = -1, r, p, v, note, vel;
  r = vf_event(e, &id);
  if (r == VF_PRESS && id >= 200 && id < 200 + VB_COUNT) { handle_button(id - 200); return; }
  if (r == VF_CHANGED && id >= 100 && id < 100 + P_COUNT) {
    p = id - 100;
    set_param(p, ui_vals[p]);
    if (p >= P_PRESET0 && p <= P_PRESET3) pad_flash[p - P_PRESET0] = 6;
    return;
  }
  if (r != VF_NO) return;

  switch (e->type) {
    case VIBE_EV_BUTTON:
      if (e->id == VB_SHIFT) shift_down = e->down ? 1 : 0;
      if (e->down) handle_button(e->id);
      break;
    case VIBE_EV_PAD:
      handle_pad(e->id, e->down, e->value);
      break;
    case VIBE_EV_NOTE:
      note = (int)e->id + ((int)ui_vals[P_OCTAVE] - 2) * 12 + (int)ui_vals[P_TRANSPOSE];
      if (note < 0) note = 0;
      if (note > 127) note = 127;
      vel = (int)(e->value * 127.0f + 0.5f);
      route_note(note, vel, e->down);
      break;
    case VIBE_EV_ENCODER:
      if (e->id >= VE_1 && e->id <= VE_4) {
        p = PAGE_PARAM[ui_page][e->id - VE_1];
        if (ui_page == UI_CHAIRS) {
          if (e->id == VE_1) p = P_SELECT;
          else p = P_PRESET0 + (int)(ui_vals[P_SELECT] + 0.5f) + (e->id - VE_2);
          if (p > P_PRESET3) p = P_LEVEL0 + (p - P_PRESET3 - 1);
        }
        set_param(p, ui_vals[p] + e->value * (PARAMS[p].steps > 1 ? 1.0f : (PARAMS[p].max - PARAMS[p].min) / 100.0f));
      } else if (e->id == VE_A) {
        next_preset(e->value >= 0 ? 1 : -1);
      } else if (e->id == VE_B) {
        set_page((ui_page + (e->value >= 0 ? 1 : UI_N - 1)) % UI_N);
      }
      break;
    case VIBE_EV_PARAM:
      /* The host may send out-of-range automation; never index a table with it. */
      apply_param((int)e->id, (float)e->value);
      break;
    case VIBE_EV_EDITOR_DATA:
      handle_editor(vibe_editor_data(), (int)e->value);
      break;
    case VIBE_EV_SAVE:
      store_state();
      break;
    default:
      break;
  }
  (void)v;
}

/* ---- audio ------------------------------------------------------------------- */
static void apply_controls(qt_ctrl *c)
{
  int i;
  c->volume = q1000(aget(&g_params[P_VOLUME]));
  c->pressure = q1000(aget(&g_params[P_PRESSURE]));
  c->position = q1000(aget(&g_params[P_POSITION]));
  c->speed = q1000(aget(&g_params[P_SPEED]));
  c->rosin = q1000(aget(&g_params[P_ROSIN]));
  c->vib_rate = q1000(aget(&g_params[P_VIB_RATE]));
  c->vib_depth = q1000(aget(&g_params[P_VIB_DEPTH]));
  c->body = q1000(aget(&g_params[P_BODY]));
  c->bridge = q1000(aget(&g_params[P_BRIDGE]));
  c->room = q1000(aget(&g_params[P_ROOM]));
  c->rv_decay = q1000(aget(&g_params[P_RV_DECAY]));
  c->rv_damp = q1000(aget(&g_params[P_RV_DAMP]));
  c->chorus_mix = q1000(aget(&g_params[P_CHORUS_MIX]));
  c->chorus_rate = q1000(aget(&g_params[P_CHORUS_RATE]));
  c->chorus_depth = q1000(aget(&g_params[P_CHORUS_DEPTH]));
  c->delay_mix = q1000(aget(&g_params[P_DELAY_MIX]));
  c->delay_time = q1000(aget(&g_params[P_DELAY_TIME]));
  c->delay_fb = q1000(aget(&g_params[P_DELAY_FB]));
  c->echo_mix = q1000(aget(&g_params[P_ECHO_MIX]));
  c->echo_time = q1000(aget(&g_params[P_ECHO_TIME]));
  c->echo_fb = q1000(aget(&g_params[P_ECHO_FB]));
  c->detune = q1000(aget(&g_params[P_DETUNE]));
  c->unison = q1000(aget(&g_params[P_UNISON]));
  c->attack = q1000(aget(&g_params[P_ATTACK]));
  c->release = q1000(aget(&g_params[P_RELEASE]));
  for (i = 0; i < QT_CHAIRS; i++) {
    c->preset[i] = param_i(P_PRESET0 + i);
    c->style[i] = param_i(P_STYLE0 + i);
    c->level[i] = aget(&g_params[P_MUTE0 + i]) > 500 ? 0.0f : q1000(aget(&g_params[P_LEVEL0 + i]));
  }
}

static void play_chord(int pad, int vel, int serial)
{
  int base = 20000 + (serial & 0x3fff) * 4;
  int dur_ms = aget(&g_params[P_CHORD_DUR]);
  int frames = (int)(((float)dur_ms * 0.001f) * sr);
  int off = ((int)q1000(aget(&g_params[P_OCTAVE])) - 2) * 12
          + (int)q1000(aget(&g_params[P_TRANSPOSE]));
  int i;
  if (pad < 0 || pad >= QT_CHORD_COUNT) pad = 0;
  if (frames < 1) frames = 1;
  if (chord_hold_n < (int)(sizeof CHORD_HOLD / sizeof CHORD_HOLD[0])) {
    CHORD_HOLD[chord_hold_n].base = base;
    CHORD_HOLD[chord_hold_n].frames = frames;
    chord_hold_n++;
  }
  for (i = 0; i < QT_CHAIRS; i++) {
    int note = QT_CHORDS[pad].note[i] + off;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (!chair_muted(i)) qt_bowed_note_on(base + i, note, vel, i);
  }
}

static void chord_process(int frames)
{
  int i, s;
  for (i = 0; i < chord_hold_n; i++) {
    CHORD_HOLD[i].frames -= frames;
    if (CHORD_HOLD[i].frames > 0) continue;
    for (s = 0; s < QT_CHAIRS; s++) qt_bowed_note_off(CHORD_HOLD[i].base + s);
    if (i + 1 < chord_hold_n) CHORD_HOLD[i] = CHORD_HOLD[chord_hold_n - 1];
    chord_hold_n--;
    i--;
  }
}

static void demo_release_all(void)
{
  int i;
  for (i = 0; i < demo_active_n; i++) qt_bowed_note_off(demo_active[i]);
  demo_active_n = 0;
  demo_off_n = 0;
}

static void demo_forget(int id)
{
  int i;
  for (i = 0; i < demo_active_n; i++) {
    if (demo_active[i] == id) {
      demo_active[i] = demo_active[demo_active_n - 1];
      demo_active_n--;
      break;
    }
  }
}

static void demo_process(float seconds)
{
  int cmd = __atomic_load_n(&demo_cmd, __ATOMIC_ACQUIRE);
  if (cmd == 1) {
    demo_index = 0;
    demo_playhead = 0.0f;
    demo_release_all();
    __atomic_store_n(&demo_cmd, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&demo_playing, 1, __ATOMIC_RELEASE);
  } else if (cmd == 2) {
    demo_release_all();
    demo_index = 0;
    __atomic_store_n(&demo_cmd, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&demo_playing, 0, __ATOMIC_RELEASE);
  }
  if (!__atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE)) return;

  demo_playhead += seconds;

  /* MIDI note lengths are real: release each voice when its duration ends
   * instead of letting the whole excerpt pile up into one sustained chord. */
  {
    int i = 0;
    while (i < demo_off_n) {
      if (demo_off[i].t <= demo_playhead) {
        int id = demo_off[i].id;
        qt_bowed_note_off(id);
        demo_forget(id);
        demo_off[i] = demo_off[demo_off_n - 1];
        demo_off_n--;
      } else {
        i++;
      }
    }
  }

  while (demo_index < QT_DEMO_N && QT_DEMO[demo_index].t <= demo_playhead) {
    const qt_demo_event *ev = &QT_DEMO[demo_index];
    int note = ev->note + ((int)q1000(aget(&g_params[P_OCTAVE])) - 2) * 12
                 + (int)q1000(aget(&g_params[P_TRANSPOSE]));
    int c = ev->chair;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (c < 0 || c >= QT_CHAIRS) c = auto_chair(note);
    if (ev->id > 0) {
      if (ev->vel > 0) {
        if (demo_active_n < (int)(sizeof demo_active / sizeof demo_active[0]))
          demo_active[demo_active_n++] = ev->id;
        if (!chair_muted(c)) qt_bowed_note_on(ev->id, note, ev->vel, c);
        if (ev->dur > 0.0f && demo_off_n < (int)(sizeof demo_off / sizeof demo_off[0])) {
          demo_off[demo_off_n].t = demo_playhead + ev->dur;
          demo_off[demo_off_n].id = ev->id;
          demo_off_n++;
        }
      } else {
        qt_bowed_note_off(ev->id);
        demo_forget(ev->id);
      }
    }
    demo_index++;
  }
  if (demo_playhead >= QT_DEMO_LEN) {
    if (aget(&g_params[P_DEMO_LOOP]) > 500) {
      demo_release_all();
      demo_index = 0;
      demo_playhead -= QT_DEMO_LEN;
    } else {
      demo_release_all();
      demo_index = QT_DEMO_N;
      __atomic_store_n(&demo_playing, 0, __ATOMIC_RELEASE);
    }
  }
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  qt_ctrl c;
  qt_qnote qn;
  volatile unsigned int r;
  float peak_l = 0.0f, peak_r = 0.0f;
  int i;
  (void)in;
  apply_controls(&c);
  qt_bowed_ctrl(&c);

  while (1) {
    VIBE_ACQUIRE();
    r = qr;
    if (r == qw) { VIBE_RELEASE(); break; }
    qn = Q[r % QN];
    qr = r + 1;
    VIBE_RELEASE();
    if (qn.type == 1) {
      int c = qn.chair;
      if (c < 0 || c >= QT_CHAIRS) c = auto_chair(qn.note);
      qt_bowed_note_on(qn.source, qn.note, qn.vel, c);
    }
    else if (qn.type == 2) qt_bowed_note_off(qn.source);
    else if (qn.type == 3) { qt_bowed_all_off(); demo_release_all(); chord_hold_n = 0; }
    else if (qn.type == 4) play_chord(qn.note, qn.vel, qn.source);
  }

  chord_process(frames);
  demo_process((float)frames / sr);
  qt_bowed_render(out_l, out_r, frames);
  for (i = 0; i < frames; i++) {
    float a = out_l[i] < 0 ? -out_l[i] : out_l[i];
    float b = out_r[i] < 0 ? -out_r[i] : out_r[i];
    if (a > peak_l) peak_l = a;
    if (b > peak_r) peak_r = b;
  }
  if (frames > 0) {
    __atomic_store_n(&meter_l, (int)(peak_l * 1000.0f), __ATOMIC_RELEASE);
    __atomic_store_n(&meter_r, (int)(peak_r * 1000.0f), __ATOMIC_RELEASE);
  }
}

/* ---- display ----------------------------------------------------------------- */
static int page_param_for_slot(int slot)
{
  int p = PAGE_PARAM[ui_page][slot];
  sel_cached = (int)(ui_vals[P_SELECT] + 0.5f);
  if (ui_page == UI_CHAIRS) {
    if (slot == 1 || slot == 2 || slot == 3)
      p = slot == 1 ? P_PRESET0 + sel_cached :
          (slot == 2 ? P_LEVEL0 + sel_cached : P_MUTE0 + sel_cached);
  }
  return p;
}

static int param_decimals(int id)
{
  float span = PARAMS[id].max - PARAMS[id].min;
  if (span <= 3.0f) return 2;
  if (span <= 100.0f) return 1;
  return 0;
}

static void draw_chairs(void)
{
  int c, i;
  char val[24];
  for (c = 0; c < QT_CHAIRS; c++) {
    int x = 12 + c * 116;
    int pr = param_i(P_PRESET0 + c);
    int mute = aget(&g_params[P_MUTE0 + c]) > 500;
    float lvl = q1000(aget(&g_params[P_LEVEL0 + c]));
    uint32_t col = c == 0 ? C_GOLD : (c == 1 ? C_AVO : (c == 2 ? C_VIOLET : C_BLUE));
    int selected = (int)(ui_vals[P_SELECT] + 0.5f) == c;
    vg_round(x, 52, 110, 62, 5, selected ? C_PANEL2 : C_PANEL);
    vg_frame(x, 52, 110, 62, selected ? 2 : 1, selected ? col : C_MUTED);
    vg_text(CHAIR_NAMES[c], x + 7, 56, VG_SMALL, mute ? C_RED : col, VG_LEFT);
    vg_text(PRESET_NAMES[pr], x + 7, 72, VG_SMALL, C_CREAM, VG_LEFT);
    vg_rect(x + 7, 93, 76, 6, C_BG);
    vg_rect(x + 7, 93, (int)(76.0f * (mute ? 0.0f : lvl)), 6, mute ? C_RED : col);
    vg_text(mute ? "MUTE" : vg_ftoa(lvl, 2, val), x + 87, 90, VG_SMALL,
            mute ? C_RED : C_MUTED, VG_RIGHT);
    for (i = 0; i < 4; i++) {
      vg_rect(x + 8 + i * 24, 100, 18, 4,
              i < qt_bowed_voices() / 4 ? col : C_PANEL2);
    }
  }
}

void vibe_on_draw(uint16_t *fb)
{
  int i, id, playing, flash_pad = -1;
  char val[24];
  float t, ml, mr;
  vg_begin(fb);

  if (help) {
    vf_begin();
    vf_enable(0);
    vf_end(C_ACC);
    vg_controls("QUARTET CONTROLS", HELP, (int)(sizeof HELP / sizeof HELP[0]) / 2);
    draw_tick++;
    if (status_pending && vibe_editor_post((const uint8_t *)status, status_len) >= 0)
      status_pending = 0;
    return;
  }
  vf_enable(1);

  vg_clear(C_BG);
  vg_rect(0, 0, 480, 46, C_PANEL);
  vg_rect(0, 45, 480, 2, C_ACC);
  vg_text("QUARTET", 14, 4, VG_BIG, C_GOLD, VG_LEFT);
  vg_text("BOWED STRINGS", 16, 30, VG_SMALL, C_MUTED, VG_LEFT);
  vg_text(CHAIR_NAMES[param_i(P_SELECT)], 210, 10, VG_BOLD, C_ACC, VG_LEFT);
  vg_text(PAGE_NAME[ui_page], 466, 7, VG_BOLD, C_CREAM, VG_RIGHT);
  playing = __atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE);
  for (i = 0; i < 16; i++) if (pad_flash[i] > 0) flash_pad = i;
  if (playing)
    vg_text(aget(&g_params[P_DEMO_LOOP]) > 500 ? "DEMO LOOP" : "DEMO",
            466, 29, VG_SMALL, C_AVO, VG_RIGHT);
  else if (flash_pad >= 0 && param_i(P_PAD_MODE) == 0)
    vg_text(QT_CHORDS[flash_pad].name, 466, 29, VG_SMALL, C_GOLD, VG_RIGHT);
  else
    vg_text(param_i(P_PAD_MODE) == 0 ? "PADS: CHORDS" : "PADS: CTRL",
            466, 29, VG_SMALL, C_MUTED, VG_RIGHT);

  vf_begin();
  draw_chairs();

  for (i = 0; i < 4; i++) {
    int y = 124 + i * 40;
    id = page_param_for_slot(i);
    vg_round(10, y, 460, 37, 5, i & 1 ? C_PANEL : C_PANEL2);
    vg_text(PARAMS[id].name, 22, y + 2, VG_SMALL, C_MUTED, VG_LEFT);
    if (PARAMS[id].steps > 1) {
      int k = (int)(ui_vals[id] + 0.5f);
      if (k < 0) k = 0;
      if (k >= PARAMS[id].steps) k = PARAMS[id].steps - 1;
      vg_text(PARAMS[id].choices[k], 458, y + 2, VG_MED, C_CREAM, VG_RIGHT);
    } else {
      vg_text(vg_ftoa(ui_vals[id], param_decimals(id), val), 458, y + 2,
              VG_BIG, C_CREAM, VG_RIGHT);
    }
    t = (ui_vals[id] - PARAMS[id].min) / (PARAMS[id].max - PARAMS[id].min);
    t = vd_clamp(t, 0.0f, 1.0f);
    vg_rect(22, y + 28, 420, 5, C_BG);
    vg_rect(22, y + 28, (int)(420.0f * t), 5, C_ACC);
    vf_value(100 + id, 10, y, 460, 37, &ui_vals[id],
             PARAMS[id].min, PARAMS[id].max,
             PARAMS[id].steps > 1 ? 1.0f : (PARAMS[id].max - PARAMS[id].min) / 100.0f);
  }

  ml = q1000(aget(&meter_l));
  mr = q1000(aget(&meter_r));
  ml = vd_clamp(ml, 0.0f, 1.0f);
  mr = vd_clamp(mr, 0.0f, 1.0f);
  vg_text("L", 14, 274, VG_SMALL, C_MUTED, VG_LEFT);
  vg_rect(28, 276, 130, 5, C_PANEL2);
  vg_rect(28, 276, (int)(130.0f * ml), 5, ml > 0.85f ? C_RED : C_AVO);
  vg_text("R", 170, 274, VG_SMALL, C_MUTED, VG_LEFT);
  vg_rect(184, 276, 130, 5, C_PANEL2);
  vg_rect(184, 276, (int)(130.0f * mr), 5, mr > 0.85f ? C_RED : C_AVO);
  vg_text("VOICES", 330, 274, VG_SMALL, C_MUTED, VG_LEFT);
  vg_text(vg_itoa(qt_bowed_voices(), val), 404, 273, VG_BOLD, C_GOLD, VG_RIGHT);
  vg_round(414, 269, 54, 10, 3, playing ? C_AVO : C_PANEL2);
  vg_text(playing ? "STOP" : "DEMO", 441, 270, VG_SMALL,
          playing ? C_BG : C_CREAM, VG_CENTER);
  vf_button(901, 414, 269, 54, 10);

  vg_rect(0, 289, 480, 2, C_ACC);
  vg_rect(0, 291, 480, 29, C_PANEL);
  for (i = 0; i < 6; i++) {
    const char *lab = (i == 2 && playing) ? "STOP" : F_LABELS[i];
    vg_text(lab, 8 + i * 79, 292, VG_SMALL, i == 1 ? C_GOLD : C_MUTED, VG_LEFT);
    vf_key(200 + VB_F1 + i, 4 + i * 79, 290, 76, 16);
  }
  for (i = 0; i < 6; i++) {
    vg_text(K_LABELS[i], 8 + i * 79, 306, VG_SMALL,
            i < 4 ? C_AVO : C_MUTED, VG_LEFT);
    vf_key(200 + VB_K1 + i, 4 + i * 79, 304, 76, 16);
  }
  vf_end(C_ACC);

  for (i = 0; i < 16; i++) {
    uint32_t col = C_PANEL2;
    float lvl = 0.18f;
    if (param_i(P_PAD_MODE) == 0) {
      col = pad_flash[i] > 0 ? C_GOLD : C_PANEL2;
      if (pad_flash[i] > 0) lvl = (float)pad_flash[i] / 6.0f;
    } else {
      if (i < 4) {
        col = i == 0 ? C_GOLD : (i == 1 ? C_AVO : (i == 2 ? C_VIOLET : C_BLUE));
        lvl = (int)(ui_vals[P_SELECT] + 0.5f) == i ? 0.8f : 0.15f;
      } else if (i >= 8 && i <= 11) {
        col = aget(&g_params[P_MUTE0 + (i - 8)]) > 500 ? C_RED : C_PANEL2;
        lvl = 0.5f;
      } else if (i >= 12 && i < 15) {
        col = (int)(ui_vals[P_SELECT] + 0.5f) == (i - 12) ? C_GOLD : C_PANEL2;
        lvl = 0.5f;
      }
    }
    if (pad_flash[i] > 0) { col = C_GOLD; lvl = (float)pad_flash[i] / 6.0f; }
    vibe_led_pad(i, col, lvl);
    if (pad_flash[i] > 0) pad_flash[i]--;
  }
  for (i = 0; i < 6; i++)
    vibe_label(VB_F1 + i, (i == 2 && playing) ? "STOP" : F_LABELS[i]);
  for (i = 0; i < 6; i++) vibe_label(VB_K1 + i, K_LABELS[i]);
  vibe_link_page(PAGE_NAME[ui_page]);
  for (i = 0; i < 4; i++) {
    int pid = page_param_for_slot(i);
    float v = (ui_vals[pid] - PARAMS[pid].min) / (PARAMS[pid].max - PARAMS[pid].min);
    vg_ftoa(vd_clamp(v, 0.0f, 1.0f), 2, val);
    vibe_link_encoder(VE_1 + i, PARAMS[pid].name, val);
  }

  draw_tick++;
  if ((draw_tick % 8) == 0) build_status();
  if (status_pending && vibe_editor_post((const uint8_t *)status, status_len) >= 0)
    status_pending = 0;
}

/* ---- init -------------------------------------------------------------------- */
void vibe_on_init(float sample_rate)
{
  int i;
  sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  qt_bowed_init(sr);
  for (i = 0; i < P_COUNT; i++) {
    ui_vals[i] = PARAMS[i].def;
    aset(&g_params[i], scaled(PARAMS[i].def));
  }
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_editor_cards(CARDS, (int)(sizeof CARDS / sizeof CARDS[0]));
  vibe_editor_accent(C_ACC);
  vibe_editor_tab_color("SOUND", C_ACC);
  vibe_editor_tab_color("BOW", C_GOLD);
  vibe_editor_tab_color("BODY", C_VIOLET);
  vibe_editor_tab_color("CHAIRS", C_BLUE);
  vibe_editor_tab_color("FX", C_VIOLET);
  vibe_editor_tab_color("STYLE", C_AVO);

  vibe_editor_control(P_SELECT, VIBE_CTL_MENU);
  vibe_editor_control(P_OCTAVE, VIBE_CTL_MENU);
  vibe_editor_control(P_TRANSPOSE, VIBE_CTL_BIPOLAR);
  vibe_editor_control(P_UNISON, VIBE_CTL_MENU);
  vibe_editor_control(P_DEMO_LOOP, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_PAD_MODE, VIBE_CTL_MENU);
  for (i = 0; i < QT_CHAIRS; i++) {
    vibe_editor_control(P_PRESET0 + i, VIBE_CTL_MENU);
    vibe_editor_control(P_MUTE0 + i, VIBE_CTL_TOGGLE);
    vibe_editor_control(P_STYLE0 + i, VIBE_CTL_MENU);
  }

  vibe_param_help(P_SELECT, "Keyboard and MIDI notes play only this slot; pads and the demo still use all four.");
  vibe_param_help(P_PRESSURE, "Bow pressure drives the friction table of the waveguide: more pressure gives a firmer, denser attack.");
  vibe_param_help(P_POSITION, "Bow position along the string. Nearer the bridge is brighter and more resistant.");
  vibe_param_help(P_SPEED, "Bow velocity. Too little gives a thin tone; too much can overdrive the string.");
  vibe_param_help(P_ROSIN, "Fine noise in the bow velocity, the grain that starts each stick-slip cycle.");
  vibe_param_help(P_VIB_RATE, "Rate of the humanised finger vibrato in Hz.");
  vibe_param_help(P_VIB_DEPTH, "Depth of the pitch modulation in cents.");
  vibe_param_help(P_BODY, "Level of the two body formants after the string waveguide.");
  vibe_param_help(P_BRIDGE, "Bridge resonance emphasis before the body filters.");
  vibe_param_help(P_UNISON, "Number of slightly detuned strings per voice: 1 solo, 2-3 section.");
  vibe_param_help(P_DETUNE, "Cents between the unison strings. A few cents make a section breathe.");
  vibe_param_help(P_PRESET0, "Eight presets: solo and section violin, viola and cello, double bass and octobass.");
  vibe_param_help(P_LEVEL0, "Per-chair mixer level. SHIFT + pads 9-12 mute a chair without losing its level.");
  vibe_param_help(P_PAD_MODE, "CHORDS: pads fire quartet chords. CTRL: shortcut layout. SHIFT swaps either way.");
  vibe_param_help(P_CHORD_DUR, "How long a pad chord is bowed before the four notes release.");
  vibe_param_help(P_CHORD_VEL, "Velocity accent of a pad chord. Higher gives a harder, denser bow attack.");
  vibe_param_help(P_ROOM, "Room reverb send. Decay and damping are on the SPACE page.");
  vibe_param_help(P_RV_DECAY, "Reverb feedback: longer decay and a denser tail.");
  vibe_param_help(P_RV_DAMP, "Reverb high-frequency damping: higher is darker and shorter.");
  vibe_param_help(P_CHORUS_MIX, "Stereo chorus crossfade, fed from the dry quartet mix.");
  vibe_param_help(P_CHORUS_RATE, "Speed of the two chorus delay modulators.");
  vibe_param_help(P_CHORUS_DEPTH, "How far the chorus delays sweep around their centre point.");
  vibe_param_help(P_DELAY_MIX, "Clean stereo delay level. Time and feedback are on the DELAY page.");
  vibe_param_help(P_DELAY_TIME, "Delay tap spacing in seconds, up to 0.6 s.");
  vibe_param_help(P_DELAY_FB, "Delay repeats; damped in the feedback path so it stays clean.");
  vibe_param_help(P_ECHO_MIX, "Two-tap echo level. Each tap is damped and slightly darker.");
  vibe_param_help(P_ECHO_TIME, "Spacing between the two echo taps in seconds.");
  vibe_param_help(P_ECHO_FB, "Echo feedback: how many bounces you hear.");
  vibe_param_help(P_STYLE0, "Articulation of this slot: bow length, pitch glide, pluck, tremolo and double stops.");

  load_state();
  vibe_notes(VIBE_NOTES);
  vibe_keys_octave(2);
  vf_mode(VF_ON_HOLD);
  vf_clicks(1);
  vf_set_focus(P_SELECT);

  qw = qr = 0;
  chord_hold_n = 0;
  chord_serial = 0;
  shift_down = 0;
  demo_index = 0;
  demo_playhead = 0.0f;
  demo_active_n = 0;
  demo_off_n = 0;
  __atomic_store_n(&demo_cmd, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&demo_playing, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&meter_l, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&meter_r, 0, __ATOMIC_RELEASE);
  for (i = 0; i < 16; i++) pad_flash[i] = 0;
  ui_page = UI_SOUND;
  help = 0;
  draw_tick = 0;
  build_status();
}
