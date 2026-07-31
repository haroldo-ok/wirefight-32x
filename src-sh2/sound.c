/*
 * WIREFIGHT 32X - PWM sound synthesizer (slave SH2)
 */
#include <stdint.h>
#include <stddef.h>
#include "32x.h"
#include "sound.h"

#define SND_RATE 11025

/* shared control block, accessed uncached from both SH2s */
static inline uint32_t f2i(uint32_t f)
{
    return (uint32_t)(((uint64_t)f << 32) / SND_RATE);
}

typedef struct {
    volatile int32_t bgm;        /* SND_SONG_*, -1 = uninitialised   */
    volatile int32_t sfx[3];     /* 0 = idle, else SND_* trigger     */
    volatile int32_t started;    /* set once master side is ready    */
} snd_ctl_t;

#define SND_CTL ((snd_ctl_t *)0x2603C000)   /* uncached SDRAM window  */
static snd_ctl_t * const ctl = SND_CTL;

/* ------------------------------------------------------------------ */
/* master side                                                         */
/* ------------------------------------------------------------------ */

static int pwm_cycle = 2088;
static int pwm_center = 1044;

void snd_init(void)
{
    /* ramp the PWM line to the centre sample to avoid power-on click.
       (kept short: pushing a fifo sample costs one pwm cycle of real
       time, so the original 23k-sample ramp would take over 2s) */
    uint16_t sample = 1, ix;
    uint32_t rep = 4;

    ctl->bgm = -1;
    ctl->sfx[0] = ctl->sfx[1] = ctl->sfx[2] = 0;
    ctl->started = 0;

    if (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT)
        pwm_cycle = (int)(((23011361u << 1) / SND_RATE + 1) >> 1) + 1;
    else
        pwm_cycle = (int)(((22801467u << 1) / SND_RATE + 1) >> 1) + 1;
    pwm_center = pwm_cycle / 2;

    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    MARS_PWM_CYCLE = (uint16_t)pwm_cycle;
    MARS_PWM_CTRL = 0x0185;   /* TM = 1, RTP, RMD = right, LMD = left */

    {
        int steps = 128, step = (pwm_center + steps - 1) / steps;
        while (sample < pwm_center) {
            for (ix = 0; ix < rep; ix++) {
                while (MARS_PWM_MONO & 0x8000);
                MARS_PWM_MONO = sample;
            }
            sample = (uint16_t)(sample + step);
        }
    }
    MARS_PWM_MONO = (uint16_t)pwm_center;
    ctl->started = 1;
}

void snd_bgm(int song)
{
    if (ctl->started)
        ctl->bgm = song;
}

void snd_play(int ch, int id)
{
    if (ch < 1 || ch > 3 || !ctl->started)
        return;
    ctl->sfx[ch - 1] = id;
}

/* ------------------------------------------------------------------ */
/* slave side: the synthesizer                                         */
/* ------------------------------------------------------------------ */

/*
 * MIDI note number -> phase increment for a 32-bit phase accumulator:
 *   inc = freq * 2^32 / rate
 * computed in fixed point at init into the note table.
 */
static uint32_t note_inc[128];

static void init_notes(void)
{
    int n;
    for (n = 0; n < 128; n++) {
        /* freq = 440 * 2^((n-69)/12) */
        int semis = n - 69;
        int oct = semis < 0 ? -((-semis) / 12 + 1) : semis / 12;
        int st = semis - oct * 12;
        /* semitone LUT x 65536 within one octave */
        static const uint32_t semi[12] = {
            65536, 69433, 73562, 77936, 82570, 87490,
            92724, 98268, 104096, 110327, 116912, 123879,
        };
        uint32_t inc = (uint32_t)(((uint64_t)440 * semi[st]) >> 16);
        if (oct > 0)
            inc <<= oct;
        else if (oct < 0)
            inc >>= -oct;
        /* inc now = freq in Hz */
        note_inc[n] = f2i(inc);
    }
}

static inline uint32_t n2i(int n)
{
    if (n < 0)
        n = 0;
    if (n > 127)
        n = 127;
    return note_inc[n];
}

typedef struct {
    uint32_t ph;      /* phase accumulator */
    uint32_t gate;    /* samples remaining */
    int      note;
    int      vol;     /* 0..32767 */
} voice_t;

typedef struct {
    uint32_t seed;
} noise_t;

static inline int32_t nz(noise_t *n)
{
    uint32_t x = n->seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    n->seed = x;
    return (int32_t)(x >> 16) - 32768;
}

/* square voice render: returns s16 */
static inline int32_t voice_render(voice_t *v, int duty)
{
    int32_t s;
    if (!v->gate)
        return 0;
    v->ph += n2i(v->note);
    s = (v->ph < (uint32_t)(0x100000000ull * duty / 256)) ? v->vol : -v->vol;
    v->gate--;
    return s;
}

/* ---------------- sfx state machines ---------------- */

typedef struct {
    int id;
    int32_t len;   /* remaining samples */
    int32_t t;     /* position */
    uint32_t ph, ph2;
    uint32_t inc, inc2;
    noise_t n;
} sfx_t;

static sfx_t sfx[3];

static void sfx_start(sfx_t *s, int id)
{
    s->id = id;
    s->t = 0;
    s->ph = s->ph2 = 0;
    s->n.seed = 0x1234567u + (uint32_t)id * 7777u;
    switch (id) {
    case SND_JAB:    s->inc = f2i(130); break;
    case SND_KICK:   s->inc = f2i(95);  break;
    case SND_GUARD:  s->inc = f2i(1750); s->inc2 = f2i(2460); break;
    case SND_BELL:   s->inc = f2i(880);  s->inc2 = f2i(660); break;
    case SND_KO:     s->inc = f2i(55);   break;
    default:         s->inc = 0; s->inc2 = 0; break;
    }
    switch (id) {
    case SND_WHOOSH: s->len = SND_RATE / 11; break;        /* ~90ms  */
    case SND_JAB:    s->len = SND_RATE / 6;  break;        /* ~170ms */
    case SND_GUARD:  s->len = SND_RATE / 7;  break;        /* ~160ms */
    case SND_BELL:   s->len = SND_RATE / 2;  break;        /* ~500ms */
    case SND_KO:     s->len = SND_RATE * 3 / 4; break;     /* ~750ms */
    case SND_WIN:    s->len = SND_RATE * 3 / 4; break;     /* ~750ms */
    case SND_KICK:   s->len = SND_RATE / 4;  break;        /* ~250ms */
    default:         s->len = 0; break;
    }
}

/* one sample of sfx; k = position in samples, id = effect */
static int32_t sfx_render(sfx_t *s)
{
    uint32_t k = (uint32_t)s->t;
    int32_t out = 0;
    uint32_t env;

    switch (s->id) {
    case SND_WHOOSH: {
        /* filtered noise burst with exp decay */
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        out = ((nz(&s->n) * (int32_t)env) >> 16);
        out = (out * 3) / 4;
        break;
    }
    case SND_JAB:
    case SND_KICK: {
        /* noise snap + low thump */
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        s->ph += s->inc;
        out = ((s->ph < 0x80000000u) ? 16000 : -16000);
        out = (int32_t)(((int64_t)out * (int32_t)env) >> 16);
        out += ((nz(&s->n) * (int32_t)env) >> 17);
        break;
    }
    case SND_GUARD: {
        /* metallic ping: two detuned sines with fast decay */
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        s->ph  += s->inc;
        s->ph2 += s->inc2;
        out = ((s->ph < 0x80000000u) ? 10000 : -10000) +
              ((s->ph2 < 0x80000000u) ? 8000 : -8000);
        out = (int32_t)(((int64_t)out * (int32_t)env * (int32_t)env) >> 32);
        break;
    }
    case SND_BELL: {
        /* classic round bell: two ding tones */
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        s->ph += k < (uint32_t)(SND_RATE / 4) ? s->inc : s->inc2;
        out = ((s->ph < 0x80000000u) ? 11000 : -11000);
        out = (int32_t)(((int64_t)out * (int32_t)env) >> 16);
        break;
    }
    case SND_KO: {
        /* deep boom */
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        s->ph += s->inc;
        out = ((s->ph < 0x80000000u) ? 18000 : -18000);
        out += (nz(&s->n) >> 1);
        out = (int32_t)(((int64_t)out * (int32_t)env * (int32_t)env) >> 32);
        break;
    }
    case SND_WIN: {
        /* C5 E5 G5 C6 fanfare */
        static const int notes[4] = { 72, 76, 79, 84 };
        uint32_t step = k / (uint32_t)(SND_RATE / 8);
        env = 65536 - (k * 65536) / (uint32_t)s->len;
        if (step > 3)
            step = 3;
        s->ph += n2i(notes[step]);
        out = ((s->ph < 0x80000000u) ? 12000 : -12000);
        out = (int32_t)(((int64_t)out * (int32_t)env) >> 16);
        break;
    }
    default:
        break;
    }
    s->t++;
    if (s->t >= s->len)
        s->id = 0;
    return out;
}

/* ---------------- bgm sequencer ---------------- */

/*
 * Tiny 2-channel chip tune: square bass + square arp, plus a noise hat.
 * Written as step tables (16 steps per bar, 8 steps per second).
 */

typedef struct {
    int8_t bass[32];   /* midi notes, -1 = rest */
    int8_t arp[32];
    uint8_t duty;
    int tempo;         /* steps per second (/2 to get 16ths at 120bpm) */
} song_t;

static song_t song_title = {
    { 45,-1,45,-1, 48,-1,45,-1, 43,-1,43,-1, 48,-1,55,-1,
      45,-1,45,-1, 48,-1,45,-1, 50,-1,50,-1, 48,-1,43,-1 },
    { 57,-1,60,-1, 64,-1,60,-1, 57,-1,60,-1, 64,-1,60,-1,
      57,-1,60,-1, 64,-1,62,-1, 60,-1,58,-1, 55,-1,58,-1 },
    128, 7,
};

static song_t song_fight = {
    { 33,-1,33,33, -1,33,-1,33, 31,-1,31,31, -1,31,-1,31,
      33,-1,33,33, -1,33,-1,33, 38,-1,38,38, 36,-1,36,-1 },
    { 57,-1,-1,57, 60,-1,-1,57, 55,-1,-1,55, 58,-1,-1,55,
      57,-1,-1,57, 60,-1,-1,57, 62,-1,-1,60, 64,-1,-1,62 },
    128, 9,
};

typedef struct {
    const song_t *song;
    int step;
    int32_t step_len, step_pos;
    voice_t vbass, varp;
    noise_t hat;
    int song_id;
} bgm_t;

static bgm_t bgm;

static void bgm_set(int id)
{
    bgm.song_id = id;
    bgm.step = 0;
    bgm.step_pos = 0;
    bgm.vbass.gate = 0;
    bgm.varp.gate = 0;
    switch (id) {
    case SND_SONG_TITLE: bgm.song = &song_title; break;
    case SND_SONG_FIGHT: bgm.song = &song_fight; break;
    default:             bgm.song = NULL; break;
    }
}

static int32_t bgm_render(void)
{
    int32_t out = 0;
    const song_t *s = bgm.song;

    if (!s)
        return 0;

    if (bgm.step_pos <= 0) {
        int b = s->bass[bgm.step];
        int a = s->arp[bgm.step];
        bgm.step_len = SND_RATE / s->tempo;
        bgm.step_pos = bgm.step_len;
        bgm.step = (bgm.step + 1) & 31;
        if (b >= 0) {
            bgm.vbass.note = b;
            bgm.vbass.vol = 5200;
            bgm.vbass.gate = (uint32_t)(bgm.step_len * 7 / 8);
        }
        if (a >= 0) {
            bgm.varp.note = a;
            bgm.varp.vol = 2600;
            bgm.varp.gate = (uint32_t)(bgm.step_len * 3 / 4);
        }
    }
    bgm.step_pos--;

    out += (voice_render(&bgm.vbass, (int)s->duty) / 3) * 2;
    out += (voice_render(&bgm.varp, (int)s->duty) / 3) * 2;
    if ((bgm.step_pos & 2047) == 0)  /* light hat every ~46ms */
        bgm.hat.seed ^= 0xB5297A4Du;
    if ((bgm.step_pos & 2047) < 128)
        out += (nz(&bgm.hat)) / 24;
    if ((bgm.step & 3) == 0 && (bgm.step_pos & 1023) < 96) /* pulse */
        out += (nz(&bgm.hat)) / 12;

    return out;
}

/* ---------------- main mixing loop (slave SH2) ---------------- */

void snd_slave(void)
{
    uint32_t frame = 0;
    int i;

    init_notes();
    bgm.song = NULL;
    bgm.song_id = 0;
    for (i = 0; i < 3; i++)
        sfx[i].id = 0;

    /* wait for the master to bring the PWM hardware up */
    while (ctl->started != 1);

    bgm_set(ctl->bgm);

    for (;;) {
        int32_t m = 0;
        int32_t q;
        int bgm_now = ctl->bgm;

        if (bgm_now != bgm.song_id)
            bgm_set(bgm_now);

        for (i = 0; i < 3; i++) {
            int trig = ctl->sfx[i];
            if (trig > 0) {
                sfx_start(&sfx[i], trig);
                ctl->sfx[i] = 0;
            }
            if (sfx[i].id)
                m += sfx_render(&sfx[i]);
        }

        m += bgm_render();

        /* clamp and centre */
        if (m > 30000)
            m = 30000;
        else if (m < -30000)
            m = -30000;
        q = pwm_center + (m >> 3);
        if (q < 4)
            q = 4;
        if (q > pwm_cycle - 4)
            q = pwm_cycle - 4;

        while (MARS_PWM_MONO & 0x8000);
        MARS_PWM_MONO = (uint16_t)q;

        frame++;
        (void)frame;
    }
}
