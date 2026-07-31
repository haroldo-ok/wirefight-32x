/*
 * WIREFIGHT 32X
 *
 * Fixed point (Q16.16) port of the Pyxel wireframe 3D fighter
 * "WIREFIGHT" by haruka_apps (original: vf_game.py, Mixamo baked
 * joint-space animations in assets_vf/motions.npz).
 *
 * Original controls: arrows/AD = move, Z/SPACE = jab, X/C = kick,
 *                    DOWN/S = guard, R = to title
 * 32X controls:      D-PAD = move/guard, A = jab, B/C = kick,
 *                    START = to title / confirm
 */
#include <stdint.h>
#include <stddef.h>
#ifdef WF_HOST
#include <stdio.h>
#endif
#include "marsl.h"
#include "gfx.h"
#include "fixed.h"
#include "data.h"
#include "sound.h"
#include "wf.h"

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

#define FPS        30
#define FX0        (165 << 16)
#define NEAR       FIXC(0.18)
#define CX         (WF_VIEW_W / 2)
#define CY         (WF_VIEW_H / 2)

/* palette indices (same order as the Pyxel palette) */
enum {
    COL_BG = 0, COL_DIM, COL_WIRE2, COL_WIRE3, COL_WIRE4, COL_WIRE5,
    COL_WIRE6, COL_FLASH, COL_HAZ, COL_SUN, COL_YEL, COL_PUR
};

static const uint8_t wf_palette_rgb[256 * 3] = {
    /* 0..15: the game palette */
    0x0A,0x0A,0x18,  0x14,0x1C,0x38,  0x23,0x3B,0x66,  0x2F,0x5E,0x9E,
    0x3F,0x8F,0xD2,  0x59,0xC8,0xF0,  0xAF,0xF6,0xFF,  0xFF,0xFF,0xFF,
    0xFF,0x4F,0xA0,  0xFF,0x9C,0x3C,  0xFF,0xD2,0x4A,  0x6B,0x3F,0xA8,
    0x10,0x1A,0x30,  0x1B,0x2A,0x4A,  0x3A,0x2A,0x55,  0x20,0x14,0x30,
    /* 16..255: zero */
};

#define RING_MIN  (-FIXC(1.85))
#define RING_MAX  ( FIXC(1.85))
#define MIN_GAP   FIXC(0.55)
#define MOVE_SPEED FIXC(0.050)
#define ROUND_FRAMES (FPS * 60)

/* attack specs (measured on the mocap data by the original author) */
enum { ATK_JAB = 0, ATK_KICK = 1 };
typedef struct {
    int hit_frame, window, cancel;
    int dmg, guard_dmg;
    fixed range, push, block_push;
    int snd;
} attack_t;

static attack_t wf_attacks[2] = {
    { 15, 3, 34,  9, 2, FIXC(0.95), FIXC(0.05), FIXC(0.08), 2 }, /* jab  */
    { 32, 4, 58, 18, 4, FIXC(1.30), FIXC(0.10), FIXC(0.16), 7 }, /* kick */
};

/* fighter states */
enum { FST_IDLE = 0, FST_MOVE, FST_JAB, FST_KICK, FST_GUARD, FST_HIT, FST_KO };

/* body parts: (jointA, jointB, half width) */
typedef struct { uint8_t a, b; fixed hw; } boxdef_t;
static boxdef_t wf_boxes[] = {
    { JI_Head, JI_HeadTop_End, FIXC(0.095) },
    { JI_Spine1, JI_Neck, FIXC(0.150) },
    { JI_Hips, JI_Spine1, FIXC(0.130) },
    { JI_LeftArm, JI_LeftForeArm, FIXC(0.052) },
    { JI_LeftForeArm, JI_LeftHand, FIXC(0.045) },
    { JI_RightArm, JI_RightForeArm, FIXC(0.052) },
    { JI_RightForeArm, JI_RightHand, FIXC(0.045) },
    { JI_LeftUpLeg, JI_LeftLeg, FIXC(0.070) },
    { JI_LeftLeg, JI_LeftFoot, FIXC(0.055) },
    { JI_RightUpLeg, JI_RightLeg, FIXC(0.070) },
    { JI_RightLeg, JI_RightFoot, FIXC(0.055) },
    { JI_LeftFoot, JI_LeftToeBase, FIXC(0.042) },
    { JI_RightFoot, JI_RightToeBase, FIXC(0.042) },
};
#define NUM_BOXES (sizeof(wf_boxes) / sizeof(wf_boxes[0]))

typedef struct { uint8_t a, b; } linedef_t;
static linedef_t wf_lines[] = {
    { JI_LeftShoulder, JI_RightShoulder },
    { JI_Neck, JI_Head },
};
#define NUM_CYLINES (sizeof(wf_lines) / sizeof(wf_lines[0]))

/* ------------------------------------------------------------------ */
/* vectors                                                             */
/* ------------------------------------------------------------------ */

typedef struct { fixed v[3]; } vec3;

static inline vec3 vec(fixed x, fixed y, fixed z)
{ vec3 r = { { x, y, z } }; return r; }

static inline void vsub(vec3 *r, const vec3 *a, const vec3 *b)
{ r->v[0] = a->v[0] - b->v[0]; r->v[1] = a->v[1] - b->v[1]; r->v[2] = a->v[2] - b->v[2]; }

static inline void vadd(vec3 *r, const vec3 *a, const vec3 *b)
{ r->v[0] = a->v[0] + b->v[0]; r->v[1] = a->v[1] + b->v[1]; r->v[2] = a->v[2] + b->v[2]; }

static inline void vmul(vec3 *r, const vec3 *a, fixed s)
{ r->v[0] = imul(a->v[0], s); r->v[1] = imul(a->v[1], s); r->v[2] = imul(a->v[2], s); }

static inline fixed vdot(const vec3 *a, const vec3 *b)
{ return imul(a->v[0], b->v[0]) + imul(a->v[1], b->v[1]) + imul(a->v[2], b->v[2]); }

static inline void vcross(vec3 *r, const vec3 *a, const vec3 *b)
{
    r->v[0] = imul(a->v[1], b->v[2]) - imul(a->v[2], b->v[1]);
    r->v[1] = imul(a->v[2], b->v[0]) - imul(a->v[0], b->v[2]);
    r->v[2] = imul(a->v[0], b->v[1]) - imul(a->v[1], b->v[0]);
}

static inline void vnorm(vec3 *r, const vec3 *a)
{
    fixed d = fixsqrt(vdot(a, a));
    if (d == 0)
        d = FIX_ONE;
    r->v[0] = fixdiv(a->v[0], d);
    r->v[1] = fixdiv(a->v[1], d);
    r->v[2] = fixdiv(a->v[2], d);
}

static inline fixed lerpfx(fixed a, fixed b, fixed t)
{ return a + imul(b - a, t); }

static inline fixed frand01(void)
{ return (fixed)((gfx_rand() >> 8) & 0xFFFF); }

static inline fixed frand(fixed a, fixed b)
{ return a + imul(b - a, frand01()); }

static inline int irand(int a, int b)
{ return a + (int)(gfx_rand() % (uint32_t)(b - a + 1)); }

/* ------------------------------------------------------------------ */
/* fighter                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    fixed x;
    int facing;
    fixed yaw;
    uint8_t col_main, col_sub;
    uint8_t is_p1;
    fixed hp;
    int state;
    uint8_t hit_done;
    uint8_t flash, guard_hold;
    int ai_timer, ai_guard_t;

    uint8_t motion;          /* MOT_* */
    const int16_t *frames;   /* current motion frames */
    uint16_t nframes;
    fixed f, rate;
    fixed root0x, root0z;

    vec3 jw[NUM_JOINTS];     /* last computed world joints */
} fighter_t;

static void f_set_motion(fighter_t *ft, int name, int start, fixed rate)
{
    const wf_motion_t *m = &wf_motions[name];
    const int16_t *h0 = m->frames + (start * NUM_JOINTS + JI_Hips) * 3;
    ft->motion = (uint8_t)name;
    ft->frames = m->frames;
    ft->nframes = m->nframes;
    ft->f = int2fix(start);
    ft->rate = rate;
    ft->root0x = ((fixed)h0[0]) << 4;
    ft->root0z = ((fixed)h0[2]) << 4;
}

static void fighter_init(fighter_t *ft, fixed x, int facing,
                         uint8_t col_main, uint8_t col_sub, int is_p1)
{
    ft->x = x;
    ft->facing = facing;
    ft->yaw = (facing > 0 ? -FIX_PI / 2 : FIX_PI / 2) - WF_BASE_YAW_Q16;
    ft->col_main = col_main;
    ft->col_sub = col_sub;
    ft->is_p1 = (uint8_t)is_p1;
    ft->hp = int2fix(100);
    ft->state = FST_IDLE;
    ft->hit_done = 0;
    ft->flash = 0;
    ft->guard_hold = 0;
    ft->ai_timer = 30;
    ft->ai_guard_t = 0;
    f_set_motion(ft, MOT_IDLE, 0, FIX_ONE);
}

static void fighter_advance(fighter_t *ft)
{
    uint32_t n = ft->nframes;
    fixed lim = ((fixed)(n - 1) << 16) - 66;   /* n - 1.001 */

    ft->f += ft->rate;
    if (ft->motion == MOT_IDLE || ft->motion == MOT_GUARD) {
        fixed per = ((fixed)(n - 1) << 16);
        while (ft->f >= per)
            ft->f -= per;
    } else if (ft->f > lim) {
        ft->f = lim;
    }
    if (ft->flash > 0)
        ft->flash--;
}

static int fighter_anim_done(const fighter_t *ft)
{
    fixed lim = ((fixed)(ft->nframes - 1) << 16) - 66;
    return ft->f >= lim;
}

static void fighter_joints(fighter_t *ft, vec3 *out)
{
    int f0 = fix2int(ft->f);
    fixed t = ft->f - int2fix(f0);
    const int16_t *a = ft->frames + f0 * NUM_JOINTS * 3;
    const int16_t *b = ft->frames +
        (f0 + 1 < (int)ft->nframes ? f0 + 1 : (int)ft->nframes - 1) * NUM_JOINTS * 3;
    int j;
    fixed hipsx, hipsz, dx, dz, c, s, ox, oz;

    for (j = 0; j < NUM_JOINTS; j++) {
        const int16_t *pa = a + j * 3;
        const int16_t *pb = b + j * 3;
        out[j].v[0] = (((fixed)pa[0]) << 4) + imul((fixed)((int)pb[0] - (int)pa[0]) << 4, t);
        out[j].v[1] = (((fixed)pa[1]) << 4) + imul((fixed)((int)pb[1] - (int)pa[1]) << 4, t);
        out[j].v[2] = (((fixed)pa[2]) << 4) + imul((fixed)((int)pb[2] - (int)pa[2]) << 4, t);
    }

    hipsx = out[JI_Hips].v[0];
    hipsz = out[JI_Hips].v[2];
    dx = hipsx - ft->root0x;
    dz = hipsz - ft->root0z;
    c = fixcos(ft->yaw);
    s = fixsin(ft->yaw);
    ox = ft->x - (imul(ft->root0x, c) + imul(ft->root0z, s));
    oz = -(-imul(ft->root0x, s) + imul(ft->root0z, c));

    for (j = 0; j < NUM_JOINTS; j++) {
        fixed lx = out[j].v[0] - dx;
        fixed lz = out[j].v[2] - dz;
        out[j].v[0] = imul(lx, c) + imul(lz, s) + ox;
        out[j].v[2] = -imul(lx, s) + imul(lz, c) + oz;
    }
}

static void f_start_attack(fighter_t *ft, int kind)
{
    ft->state = kind == ATK_JAB ? FST_JAB : FST_KICK;
    f_set_motion(ft, kind == ATK_JAB ? MOT_JAB : MOT_KICK, 0, FIX_ONE);
    ft->hit_done = 0;
}

static void f_start_guard(fighter_t *ft)
{
    ft->state = FST_GUARD;
    f_set_motion(ft, MOT_GUARD, 0, FIX_ONE);
}

static void f_start_hit(fighter_t *ft)
{
    ft->state = FST_HIT;
    f_set_motion(ft, MOT_HIT, 0, FIXC(1.3));
    ft->flash = 6;
}

static void f_start_ko(fighter_t *ft)
{
    ft->state = FST_KO;
    f_set_motion(ft, MOT_KO, 52, FIX_ONE);
    ft->flash = 8;
}

#ifdef WF_TESTBUILD
#define WF_RNG_SEED 0x00C0FFEEu
#else
#define WF_RNG_SEED (mars_vblank_count * 2654435761u + 0x9E3779B9u)
#endif

/* ------------------------------------------------------------------ */
/* sparks                                                              */
/* ------------------------------------------------------------------ */

#define MAX_SPARKS 40
typedef struct {
    vec3 p, v;
    int16_t life;
    uint8_t col;
    uint8_t used;
} spark_t;

/* ------------------------------------------------------------------ */
/* state of the whole game                                             */
/* ------------------------------------------------------------------ */

enum { GS_TITLE = 0, GS_PLAY, GS_RESULT };
enum { CAM_INTRO = 0, CAM_SIDE, CAM_KO };

typedef struct {
    int state;
    uint32_t t;

    fighter_t p1o, p2o;
    spark_t sparks[MAX_SPARKS];
    int shake, hitstop;
    char bigtext[12];
    int bigtext_life;
    int cam_mode;
    vec3 cam_pos, cam_tgt;
    fixed timer;
    int result_wait;
    char result_msg[24];
    fixed demo_master;
    int demo_i;
    fighter_t *ko_victim;
    int bgm_track;       /* SND_SONG_* or -1 */

    /* camera basis */
    vec3 cam, cr, cu, cf;

    /* input */
    uint16_t pad, pad_prev;
} app_t;

static app_t G;

/* ------------------------------------------------------------------ */
/* sounds (thin mapper over the original pyxel channel usage)          */
/* ------------------------------------------------------------------ */

static void set_bgm(app_t *app, int track)
{
    if (app->bgm_track == track)
        return;
    app->bgm_track = track;
    snd_bgm(track);
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

#define PAD_CUR   (G.pad)
#define PAD_PREV  (G.pad_prev)
#define BTNP(b)  ((PAD_CUR & (b)) && !(PAD_PREV & (b)))

static int in_left(void)  { return (PAD_CUR & SEGA_CTRL_LEFT) != 0; }
static int in_right(void) { return (PAD_CUR & SEGA_CTRL_RIGHT) != 0; }
static int in_guard(void) { return (PAD_CUR & SEGA_CTRL_DOWN) != 0; }
static int in_jab(void)   { return BTNP(SEGA_CTRL_A) || BTNP(SEGA_CTRL_Z); }
static int in_kick(void)  { return BTNP(SEGA_CTRL_B) || BTNP(SEGA_CTRL_C) || BTNP(SEGA_CTRL_X); }
static int in_start(void)
{
    return BTNP(SEGA_CTRL_A) || BTNP(SEGA_CTRL_Z) || BTNP(SEGA_CTRL_START)
        || BTNP(SEGA_CTRL_C);
}
static int in_reset(void) { return BTNP(SEGA_CTRL_START) || BTNP(SEGA_CTRL_MODE); }

/* ------------------------------------------------------------------ */
/* game state transitions                                              */
/* ------------------------------------------------------------------ */

static void new_fighters(app_t *app)
{
    fighter_init(&app->p1o, FIXC(-0.9), +1, COL_WIRE6, COL_WIRE4, 1);
    fighter_init(&app->p2o, FIXC(+0.9), -1, COL_SUN, COL_HAZ, 0);
    {
        int i;
        for (i = 0; i < MAX_SPARKS; i++)
            app->sparks[i].used = 0;
    }
    app->shake = 0;
    app->hitstop = 0;
    app->bigtext[0] = 0;
    app->bigtext_life = 0;
    app->cam_mode = CAM_INTRO;
    app->cam_pos = vec(FIXC(-2.8), FIXC(1.9), FIXC(-3.4));
    app->cam_tgt = vec(FIXC(0.0), FIXC(1.1), FIXC(0.0));
}

static void to_title(app_t *app)
{
    app->state = GS_TITLE;
    app->t = 0;
    new_fighters(app);
    app->demo_master = 0;
    app->demo_i = 0;
    set_bgm(app, SND_SONG_TITLE);
}

static void to_play(app_t *app)
{
    app->state = GS_PLAY;
    app->t = 0;
    new_fighters(app);
    app->cam_mode = CAM_SIDE;
    app->timer = int2fix(ROUND_FRAMES);
    {
        static const char s[] = "FIGHT!";
        int i;
        for (i = 0; s[i]; i++) app->bigtext[i] = s[i];
        app->bigtext[6] = 0;
    }
    app->bigtext_life = 40;
    app->result_wait = 0;
    set_bgm(app, SND_SONG_FIGHT);
    snd_play(1, SND_BELL);
}

static void to_result(app_t *app, const char *msg)
{
    int i;
    app->state = GS_RESULT;
    app->t = 0;
    for (i = 0; msg[i] && i < (int)sizeof(app->result_msg) - 1; i++)
        app->result_msg[i] = msg[i];
    app->result_msg[i] = 0;
    /* "YOU WIN!" -> win fanfare */
    {
        const char *p = app->result_msg;
        while (*p) {
            if (p[0] == 'Y' && p[1] == 'O' && p[2] == 'U' && p[4] == 'W') {
                snd_play(2, SND_WIN);
                break;
            }
            p++;
        }
    }
}

#ifdef WF_TESTBUILD
#define WF_RNG_SEED 0x00C0FFEEu
#else
#define WF_RNG_SEED (mars_vblank_count * 2654435761u + 0x9E3779B9u)
#endif

/* ------------------------------------------------------------------ */
/* sparks                                                              */
/* ------------------------------------------------------------------ */

static void spawn_sparks(app_t *app, const vec3 *p, int n, uint8_t col)
{
    int i, k;
    for (i = 0; i < n; i++) {
        spark_t *sp = NULL;
        fixed a, bb, spd;
        for (k = 0; k < MAX_SPARKS; k++)
            if (!app->sparks[k].used) { sp = &app->sparks[k]; break; }
        if (!sp)
            return;
        a = (fixed)((((int64_t)(gfx_rand() & 0xFFFF)) * FIX_TWO_PI) >> 16);
        bb = frand(FIXC(-1.0), FIXC(1.0));
        spd = frand(FIXC(0.03), FIXC(0.10));
        sp->used = 1;
        sp->p = *p;
        sp->v.v[0] = imul(fixcos(a), spd);
        sp->v.v[1] = imul(imul(bb, spd), FIXC(0.8));
        sp->v.v[2] = imul(fixsin(a), spd);
        sp->life = (int16_t)irand(8, 18);
        sp->col = col;
    }
}

static void update_sparks(app_t *app)
{
    int i;
    for (i = 0; i < MAX_SPARKS; i++) {
        spark_t *sp = &app->sparks[i];
        if (!sp->used)
            continue;
        vadd(&sp->p, &sp->p, &sp->v);
        sp->v.v[0] = imul(sp->v.v[0], FIXC(0.9));
        sp->v.v[1] = imul(sp->v.v[1], FIXC(0.9));
        sp->v.v[2] = imul(sp->v.v[2], FIXC(0.9));
        if (--sp->life <= 0)
            sp->used = 0;
    }
}

/* ------------------------------------------------------------------ */
/* attract mode (title demo)                                           */
/* ------------------------------------------------------------------ */

enum { DE_MOTION, DE_HIT, DE_BLOCK, DE_KO, DE_BIGTEXT, DE_CAMSIDE, DE_RESET };

typedef struct { int16_t time; uint8_t kind, who; int8_t arg; fixed rate; } demo_ev_t;

/* who: 0 = p1, 1 = p2 ; for DE_BIGTEXT: who = 0 "K.O." only used */
static demo_ev_t wf_demo[] = {
    {  70, DE_MOTION, 0, MOT_JAB,  FIX_ONE },
    {  86, DE_HIT,    1, 26,       0 },
    { 128, DE_MOTION, 0, MOT_IDLE, FIX_ONE },
    { 138, DE_MOTION, 1, MOT_IDLE, FIX_ONE },
    { 175, DE_MOTION, 1, MOT_KICK, FIX_ONE },
    { 188, DE_MOTION, 0, MOT_GUARD,FIX_ONE },
    { 212, DE_BLOCK,  0, 0,        0 },
    { 255, DE_MOTION, 1, MOT_IDLE, FIX_ONE },
    { 262, DE_MOTION, 0, MOT_IDLE, FIX_ONE },
    { 300, DE_MOTION, 0, MOT_KICK, FIX_ONE },
    { 338, DE_KO,     1, 100,      0 },
    { 365, DE_BIGTEXT,0, 0,        0 },
    { 428, DE_CAMSIDE,0, 0,        0 },
    { 505, DE_RESET,  0, 0,        0 },
};
#define WF_DEMO_LEN (sizeof(wf_demo) / sizeof(wf_demo[0]))

static void apply_demo_event(app_t *app, const demo_ev_t *ev)
{
    fighter_t *actor = ev->who ? &app->p2o : &app->p1o;

    switch (ev->kind) {
    case DE_MOTION:
        f_set_motion(actor, ev->arg, 0, ev->rate);
        break;
    case DE_HIT:
        f_start_hit(actor);
        app->hitstop = 5;
        app->shake = 5;
        fighter_joints(actor, actor->jw);
        spawn_sparks(app, &actor->jw[JI_Spine1], 14, COL_YEL);
        snd_play(2, SND_JAB);
        break;
    case DE_BLOCK:
        fighter_joints(actor, actor->jw);
        spawn_sparks(app, &actor->jw[JI_LeftForeArm], 6, COL_WIRE5);
        app->shake = 2;
        snd_play(3, SND_GUARD);
        break;
    case DE_KO:
        f_start_ko(actor);
        app->hitstop = 8;
        app->shake = 8;
        app->cam_mode = CAM_KO;
        app->ko_victim = actor;
        fighter_joints(actor, actor->jw);
        spawn_sparks(app, &actor->jw[JI_Head], 22, COL_FLASH);
        snd_play(2, SND_JAB);
        snd_play(3, SND_KO);
        break;
    case DE_BIGTEXT:
        /* K.O. text */
        {
            static const char s[] = "K.O.";
            int i;
            for (i = 0; s[i]; i++) app->bigtext[i] = s[i];
            app->bigtext[4] = 0;
            app->bigtext_life = 999;
        }
        break;
    case DE_CAMSIDE:
        app->cam_mode = CAM_SIDE;
        break;
    case DE_RESET:
        new_fighters(app);
        app->demo_master = 0;
        app->demo_i = 0;
        break;
    default:
        break;
    }
}

static void update_bigtext(app_t *app)
{
    if (app->bigtext_life > -900)
        app->bigtext_life--;
}

static void update_title_demo(app_t *app)
{
    if (app->shake > 0)
        app->shake--;
    update_sparks(app);
    update_bigtext(app);
    if (app->hitstop > 0) {
        app->hitstop--;
    } else {
        app->demo_master += FIX_ONE;
        fighter_advance(&app->p1o);
        fighter_advance(&app->p2o);
        while (app->demo_i < (int)WF_DEMO_LEN &&
               int2fix(wf_demo[app->demo_i].time) <= app->demo_master) {
            apply_demo_event(app, &wf_demo[app->demo_i]);
            app->demo_i++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* movement / AI / attack resolution                                   */
/* ------------------------------------------------------------------ */

static void try_move(app_t *app, fighter_t *f, fixed direction)
{
    fixed nx = f->x + imul(direction, MOVE_SPEED);
    if (f->is_p1) {
        fixed maxx = app->p2o.x - MIN_GAP;
        if (nx > maxx) nx = maxx;
        if (nx < RING_MIN) nx = RING_MIN;
    } else {
        fixed minx = app->p1o.x + MIN_GAP;
        if (nx < minx) nx = minx;
        if (nx > RING_MAX) nx = RING_MAX;
    }
    f->x = nx;
}

static void update_input(app_t *app, fighter_t *f)
{
    if (f->state == FST_IDLE || f->state == FST_MOVE) {
        int move = (in_right() ? 1 : 0) - (in_left() ? 1 : 0);
        if (in_guard()) {
            f_start_guard(f);
        } else if (in_jab()) {
            f_start_attack(f, ATK_JAB);
            snd_play(1, SND_WHOOSH);
        } else if (in_kick()) {
            f_start_attack(f, ATK_KICK);
            snd_play(1, SND_WHOOSH);
        } else if (move != 0) {
            try_move(app, f, int2fix(move));
            f->state = FST_MOVE;
        } else {
            f->state = FST_IDLE;
        }
    } else if (f->state == FST_GUARD) {
        if (in_guard()) {
            int mv = (in_right() ? 1 : 0) - (in_left() ? 1 : 0);
            if (mv != 0)
                try_move(app, f, imul(int2fix(mv), FIX_HALF));
        } else {
            f->state = FST_IDLE;
            f_set_motion(f, MOT_IDLE, 0, FIX_ONE);
        }
    }
}

static void update_ai(app_t *app, fighter_t *ai)
{
    fighter_t *opp = &app->p1o;
    fixed dist = opp->x > ai->x ? opp->x - ai->x : ai->x - opp->x;
    int r;

#ifdef WF_TESTBUILD
    /* test builds: holding MODE on controller 2 freezes the AI so the
     * automated point-to-point tests are deterministic */
    if (Mars_ReadController(1) & SEGA_CTRL_MODE)
        return;
#endif

    if (ai->state != FST_IDLE && ai->state != FST_MOVE) {
        if (ai->state == FST_GUARD && frand01() < FIXC(0.03)) {
            ai->state = FST_IDLE;
            f_set_motion(ai, MOT_IDLE, 0, FIX_ONE);
        }
        return;
    }

    /* react to incoming attacks */
    if ((opp->state == FST_JAB || opp->state == FST_KICK)) {
        const attack_t *spec = &wf_attacks[opp->state == FST_JAB ? ATK_JAB : ATK_KICK];
        if (dist < spec->range + FIXC(0.15) &&
            fix2int(opp->f) < spec->hit_frame - 2 &&
            frand01() < FIXC(0.30)) {
            f_start_guard(ai);
            return;
        }
    }

    ai->ai_timer--;
    if (ai->ai_timer > 0) {
        if (ai->state == FST_MOVE && dist > MIN_GAP) {
            fixed direction = opp->x > ai->x ? FIX_ONE : -FIX_ONE;
            try_move(app, ai, direction);
        }
        return;
    }
    ai->ai_timer = irand(14, 30);

    r = -1;
    if (dist <= wf_attacks[ATK_JAB].range) {
        fixed rr = frand01();
        if (rr < FIXC(0.45))
            r = 0;
        else if (rr < FIXC(0.65))
            r = 1;
        else if (rr < FIXC(0.80))
            r = 2;
        else
            r = 3;
    } else if (dist <= wf_attacks[ATK_KICK].range) {
        fixed rr = frand01();
        if (rr < FIXC(0.40))
            r = 1;
        else if (rr < FIXC(0.75))
            r = 3;
        else
            r = 2;
    } else {
        ai->state = FST_MOVE;
        return;
    }

    switch (r) {
    case 0: f_start_attack(ai, ATK_JAB); break;
    case 1: f_start_attack(ai, ATK_KICK); break;
    case 2: f_start_guard(ai); break;
    default:
        ai->state = FST_MOVE;
        try_move(app, ai, opp->x > ai->x ? -FIX_ONE : FIX_ONE);
        break;
    }
}

static void resolve_attack(app_t *app, fighter_t *atk, fighter_t *dfn)
{
    const attack_t *spec;
    fixed dist, d;
    int blocked;
    fixed push_dir;

    if (atk->state != FST_JAB && atk->state != FST_KICK)
        return;
    if (atk->hit_done)
        return;
    spec = &wf_attacks[atk->state == FST_JAB ? ATK_JAB : ATK_KICK];

    /* |atk.f - hit_frame| <= window */
    d = atk->f - int2fix(spec->hit_frame);
    if (d < 0) d = -d;
    if (d > int2fix(spec->window))
        return;

    dist = atk->x > dfn->x ? atk->x - dfn->x : dfn->x - atk->x;
    if (dist > spec->range)
        return;
    if (dfn->state == FST_HIT || dfn->state == FST_KO)
        return;

    atk->hit_done = 1;
    blocked = (dfn->state == FST_GUARD);
    push_dir = dfn->x > atk->x ? FIX_ONE : -FIX_ONE;

    if (blocked) {
        dfn->hp -= int2fix(spec->guard_dmg);
        dfn->x += imul(push_dir, spec->block_push);
        if (dfn->x > RING_MAX) dfn->x = RING_MAX;
        if (dfn->x < RING_MIN) dfn->x = RING_MIN;
        fighter_joints(dfn, dfn->jw);
        spawn_sparks(app, &dfn->jw[JI_LeftForeArm], 6, COL_WIRE5);
        app->shake = 2;
        snd_play(3, SND_GUARD);
    } else {
        dfn->hp -= int2fix(spec->dmg);
        dfn->x += imul(push_dir, spec->push);
        if (dfn->x > RING_MAX) dfn->x = RING_MAX;
        if (dfn->x < RING_MIN) dfn->x = RING_MIN;
        fighter_joints(dfn, dfn->jw);
        spawn_sparks(app, &dfn->jw[JI_Spine1], 14, COL_YEL);
        app->shake = 5;
        app->hitstop = 5;
        snd_play(2, spec->snd);
        if (dfn->hp <= 0) {
            dfn->hp = 0;
            f_start_ko(dfn);
            app->result_wait = 95;
            app->cam_mode = CAM_KO;
            app->ko_victim = dfn;
            spawn_sparks(app, &dfn->jw[JI_Head], 20, COL_FLASH);
            app->shake = 8;
            app->hitstop = 8;
            snd_play(3, SND_KO);
            {
                static const char s[] = "K.O.";
                int i;
                for (i = 0; s[i]; i++) app->bigtext[i] = s[i];
                app->bigtext[4] = 0;
                app->bigtext_life = 999;
            }
        } else {
            f_start_hit(dfn);
        }
    }
}

static void finish_actions(fighter_t *f)
{
    if (f->state == FST_JAB || f->state == FST_KICK) {
        const attack_t *spec = &wf_attacks[f->state == FST_JAB ? ATK_JAB : ATK_KICK];
        if (fix2int(f->f) >= spec->cancel || fighter_anim_done(f)) {
            f->state = FST_IDLE;
            f_set_motion(f, MOT_IDLE, 0, FIX_ONE);
        }
    } else if (f->state == FST_HIT) {
        if (fighter_anim_done(f)) {
            f->state = FST_IDLE;
            f_set_motion(f, MOT_IDLE, 0, FIX_ONE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* update                                                              */
/* ------------------------------------------------------------------ */

static void update_play(app_t *app)
{
    if (app->shake > 0)
        app->shake--;
    update_sparks(app);
    update_bigtext(app);

    if (app->p1o.state == FST_KO || app->p2o.state == FST_KO) {
        fighter_advance(&app->p1o);
        fighter_advance(&app->p2o);
        app->result_wait--;
        if (app->result_wait <= 0) {
            int winner_is_p1 = app->p1o.state != FST_KO;
            to_result(app, winner_is_p1 ? "YOU WIN!" : "YOU LOSE");
        }
        return;
    }

    if (app->hitstop > 0) {
        app->hitstop--;
        return;
    }

    app->timer -= FIX_ONE;
    if (app->timer <= 0) {
        if (app->p1o.hp > app->p2o.hp)
            to_result(app, "TIME UP - YOU WIN!");
        else if (app->p2o.hp > app->p1o.hp)
            to_result(app, "TIME UP - YOU LOSE");
        else
            to_result(app, "TIME UP - DRAW");
        return;
    }

    update_input(app, &app->p1o);
    update_ai(app, &app->p2o);
    fighter_advance(&app->p1o);
    fighter_advance(&app->p2o);
    resolve_attack(app, &app->p1o, &app->p2o);
    resolve_attack(app, &app->p2o, &app->p1o);
    finish_actions(&app->p1o);
    finish_actions(&app->p2o);
}

static void update(app_t *app)
{
    app->t++;
    switch (app->state) {
    case GS_TITLE:
        update_title_demo(app);
        if (in_start())
            to_play(app);
        break;
    case GS_PLAY:
        update_play(app);
        if (in_reset())
            to_title(app);
        break;
    case GS_RESULT:
    default:
        if (app->t > 25 && (in_start() || in_reset()))
            to_title(app);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */

static void update_camera(app_t *app)
{
    vec3 want = app->cam_pos, tgt = app->cam_tgt;
    fixed k;

    switch (app->cam_mode) {
    case CAM_INTRO: {
        fixed a = FIXC(3.6) - imul((fixed)(int32_t)(app->t << 16), FIXC(0.006));
        want = vec(imul(fixcos(a), FIXC(3.4)), FIXC(1.5), imul(fixsin(a), FIXC(3.4)));
        tgt = vec(0, FIXC(1.05), 0);
        if (app->demo_master > int2fix(55))
            app->cam_mode = CAM_SIDE;
        break;
    }
    case CAM_SIDE: {
        fixed mid = imul(app->p1o.x + app->p2o.x, FIX_HALF);
        want = vec(imul(mid, FIXC(0.4)), FIXC(1.25), FIXC(-3.15));
        tgt = vec(imul(mid, FIXC(0.6)), FIX_ONE, 0);
        break;
    }
    case CAM_KO: {
        fixed a = FIXC(-1.6) + imul((fixed)(int32_t)(app->t << 16), FIXC(0.008));
        vec3 v;
        fighter_joints(app->ko_victim, app->ko_victim->jw);
        v = app->ko_victim->jw[JI_Hips];
        want = vec(v.v[0] + imul(fixcos(a), FIXC(3.0)), FIXC(1.35),
                   v.v[2] + imul(fixsin(a), FIXC(3.0)));
        tgt = vec(v.v[0], FIXC(0.7), v.v[2]);
        break;
    }
    }

    k = app->cam_mode == CAM_INTRO ? FIX_ONE : FIXC(0.10);
    app->cam_pos.v[0] = lerpfx(app->cam_pos.v[0], want.v[0], k);
    app->cam_pos.v[1] = lerpfx(app->cam_pos.v[1], want.v[1], k);
    app->cam_pos.v[2] = lerpfx(app->cam_pos.v[2], want.v[2], k);
    app->cam_tgt.v[0] = lerpfx(app->cam_tgt.v[0], tgt.v[0], k);
    app->cam_tgt.v[1] = lerpfx(app->cam_tgt.v[1], tgt.v[1], k);
    app->cam_tgt.v[2] = lerpfx(app->cam_tgt.v[2], tgt.v[2], k);
}

static void setup_view(app_t *app)
{
    static const vec3 UP = { { 0, FIXC(1.0), 0 } };
    vec3 f, r, u, tmp;

    app->cam = app->cam_pos;
    if (app->shake > 0) {
        app->cam.v[0] += frand(FIXC(-0.03), FIXC(0.03));
        app->cam.v[1] += frand(FIXC(-0.03), FIXC(0.03));
    }
    vsub(&tmp, &app->cam_tgt, &app->cam);
    vnorm(&f, &tmp);
    vcross(&tmp, &UP, &f);
    vnorm(&r, &tmp);
    vcross(&u, &f, &r);
    app->cf = f;
    app->cr = r;
    app->cu = u;
}

static inline vec3 camera_space(app_t *app, const vec3 *p)
{
    vec3 d, r;
    vsub(&d, p, &app->cam);
    r.v[0] = vdot(&d, &app->cr);
    r.v[1] = vdot(&d, &app->cu);
    r.v[2] = vdot(&d, &app->cf);
    return r;
}

/* ------------------------------------------------------------------ */
/* draw helpers                                                        */
/* ------------------------------------------------------------------ */

#if defined(WF_TESTBUILD) || defined(WF_HOST)
/* white-box device stats, published on the COMM ports each frame and
 * visible in PicoDrive's register-access log (or stderr on host) */
int wf_dbg_plots, wf_dbg_lines, wf_dbg_maxc;
static void dbg_reset_stats(void) { wf_dbg_plots = 0; wf_dbg_lines = 0; wf_dbg_maxc = 0; }
static void dbg_report_stats(void)
{
#ifdef WF_HOST
    fprintf(stderr, "      stats: lines=%d plots=%d maxc=%d\n",
            wf_dbg_lines, wf_dbg_plots, wf_dbg_maxc);
#else
    MARS_SYS_COMM6  = (uint16_t)wf_dbg_lines;
    MARS_SYS_COMM8  = (uint16_t)(wf_dbg_plots & 0xFFFF);
    MARS_SYS_COMM10 = (uint16_t)(wf_dbg_maxc < 0 ? -wf_dbg_maxc : wf_dbg_maxc);
#endif
}

/* perf telemetry: vblank deltas around the two halves of a frame */
static unsigned dbg_vb_anchor, dbg_vb_anchor2;
static void dbg_mark_vb(void)
{
#ifndef WF_HOST
    dbg_vb_anchor2 = mars_vblank_count;
    MARS_SYS_COMM2  = (uint16_t)(dbg_vb_anchor2 - dbg_vb_anchor);
    dbg_vb_anchor   = dbg_vb_anchor2;
#endif
}
static void dbg_mark_vb2(void)
{
#ifndef WF_HOST
    unsigned now = mars_vblank_count;
    MARS_SYS_COMM4  = (uint16_t)(now - dbg_vb_anchor);
#endif
}
#endif

static void draw_seg(app_t *app, vec3 a, vec3 b, uint8_t col)
{
    fixed za = a.v[2], zb = b.v[2];
    int x1, y1, x2, y2;

    if (za < NEAR && zb < NEAR)
        return;
    if (za < NEAR || zb < NEAR) {
        fixed tt = fixdiv(NEAR - za, zb - za);
        vec3 c;
        c.v[0] = a.v[0] + imul(b.v[0] - a.v[0], tt);
        c.v[1] = a.v[1] + imul(b.v[1] - a.v[1], tt);
        c.v[2] = NEAR;
        if (za < NEAR)
            a = c;
        else
            b = c;
    }
    {
        fixed p;
        p = fixdiv(a.v[0], a.v[2]);
        x1 = CX + fix2int(imul((fixed)FX0, p));
        p = fixdiv(a.v[1], a.v[2]);
        y1 = CY - fix2int(imul((fixed)FX0, p));
        p = fixdiv(b.v[0], b.v[2]);
        x2 = CX + fix2int(imul((fixed)FX0, p));
        p = fixdiv(b.v[1], b.v[2]);
        y2 = CY - fix2int(imul((fixed)FX0, p));
    }
    if ((x1 < -80 && x2 < -80) || (x1 > WF_VIEW_W + 80 && x2 > WF_VIEW_W + 80))
        return;
#ifdef WF_HOST
    if (x1 < -30000 || x1 > 30000 || y1 < -30000 || y1 > 30000 ||
        x2 < -30000 || x2 > 30000 || y2 < -30000 || y2 > 30000) {
        fprintf(stderr, "HUGE seg (%d,%d)-(%d,%d) from a(%d,%d,%d) b(%d,%d,%d) col=%d\n",
                x1, y1, x2, y2, a.v[0], a.v[1], a.v[2], b.v[0], b.v[1], b.v[2], col);
    }
#endif
    gfx_line(x1, y1, x2, y2, col);
}

static void draw_box(app_t *app, const vec3 *a, const vec3 *b, fixed hw, uint8_t col)
{
    static const vec3 REFY = { { 0, FIXC(1.0), 0 } };
    static const vec3 REFX = { { FIXC(1.0), 0, 0 } };
    vec3 n, r, u, tmp, cor[8];
    const vec3 *ref;
    int e, i, j, c = 0;
    static const int8_t sr_[4] = { -1, 1, 1, -1 };
    static const int8_t su_[4] = { -1, -1, 1, 1 };

    vsub(&tmp, b, a);
    vnorm(&n, &tmp);
    if (n.v[1] < FIXC(0.9) && n.v[1] > -FIXC(0.9))
        ref = &REFY;
    else
        ref = &REFX;
    vcross(&tmp, ref, &n);
    vnorm(&r, &tmp);
    vcross(&u, &n, &r);

    for (e = 0; e < 2; e++) {
        const vec3 *end = e ? b : a;
        for (i = 0; i < 4; i++, c++) {
            fixed sr = sr_[i] < 0 ? -hw : hw;
            fixed su = su_[i] < 0 ? -hw : hw;
            vec3 t1, t2, t3;
            vmul(&t1, &r, sr);
            vmul(&t2, &u, su);
            vadd(&t3, end, &t1);
            vadd(&t3, &t3, &t2);
            cor[c] = camera_space(app, &t3);
        }
    }
    for (i = 0; i < 4; i++) {
        j = (i + 1) & 3;
        draw_seg(app, cor[i], cor[j], col);
        draw_seg(app, cor[4 + i], cor[4 + j], col);
        draw_seg(app, cor[i], cor[4 + i], col);
    }
}

/* ------------------------------------------------------------------ */
/* stage / fighter / sparks drawing                                    */
/* ------------------------------------------------------------------ */

static void draw_stage(app_t *app)
{
    int i, k;
    vec3 a, b;

    /* stars */
    for (i = 0; i < 36; i++)
        gfx_pset((i * 67) % WF_VIEW_W, (i * 41) % (WF_VIEW_H / 2),
                 i % 3 ? COL_DIM : COL_WIRE3);

    /* floor grid */
    for (k = -4; k <= 4; k++) {
        a = vec(int2fix(k) * 16 / 10, FIXC(-0.42), FIXC(-6.5));
        b = vec(int2fix(k) * 16 / 10, FIXC(-0.42), FIXC( 6.5));
        draw_seg(app, camera_space(app, &a), camera_space(app, &b), COL_DIM);
        a = vec(FIXC(-6.5), FIXC(-0.42), int2fix(k) * 16 / 10);
        b = vec(FIXC( 6.5), FIXC(-0.42), int2fix(k) * 16 / 10);
        draw_seg(app, camera_space(app, &a), camera_space(app, &b), COL_DIM);
    }

    /* the ring platform */
    {
        const fixed hf = FIXC(2.2);
        vec3 top[4], bot[4];
        top[0] = vec(-hf, 0, -hf); top[1] = vec(hf, 0, -hf);
        top[2] = vec(hf, 0, hf);   top[3] = vec(-hf, 0, hf);
        for (i = 0; i < 4; i++)
            bot[i] = vec(top[i].v[0], FIXC(-0.42), top[i].v[2]);
        for (i = 0; i < 4; i++) {
            {
                int j = (i + 1) & 3;
                draw_seg(app, camera_space(app, &top[i]), camera_space(app, &top[j]), COL_WIRE5);
                draw_seg(app, camera_space(app, &bot[i]), camera_space(app, &bot[j]), COL_WIRE3);
                draw_seg(app, camera_space(app, &top[i]), camera_space(app, &bot[i]), COL_WIRE3);
            }
        }
        {
            const int n = 8;
            int kk;
            for (kk = 1; kk < n; kk++) {
                fixed t = -hf + ((int64_t)2 * hf * kk) / n;
                a = vec(t, 0, -hf); b = vec(t, 0, hf);
                draw_seg(app, camera_space(app, &a), camera_space(app, &b), COL_WIRE2);
                a = vec(-hf, 0, t); b = vec(hf, 0, t);
                draw_seg(app, camera_space(app, &a), camera_space(app, &b), COL_WIRE2);
            }
        }
    }
}

static void draw_fighter(app_t *app, fighter_t *fi)
{
    const vec3 *js;
    uint8_t col;
    vec3 sc;
    unsigned i;

#ifndef WF_BENCH_NOPOSE
    fighter_joints(fi, fi->jw);
#endif
#ifdef WF_HOST_TRACE
fprintf(stderr, "    joints done: hips=(%d,%d,%d) f=%d nfr=%d mot=%d\n",
fi->jw[0].v[0], fi->jw[0].v[1], fi->jw[0].v[2], fi->f, fi->nframes, fi->motion); fflush(stderr);
#endif
    js = fi->jw;

    sc = camera_space(app, &js[JI_Hips]);
    col = fi->flash > 0 ? COL_FLASH : fi->col_main;

    /* shadow */
#ifndef WF_BENCH_NOSHADOW
    {
        const fixed sh = FIXC(0.32);
        vec3 pts[4];
        vec3 hx = js[JI_Hips];
        hx.v[1] = FIXC(0.01);
        pts[0] = vec(hx.v[0] - sh, hx.v[1], hx.v[2]);
        pts[1] = vec(hx.v[0], hx.v[1], hx.v[2] - sh);
        pts[2] = vec(hx.v[0] + sh, hx.v[1], hx.v[2]);
        pts[3] = vec(hx.v[0], hx.v[1], hx.v[2] + sh);
        for (i = 0; i < 4; i++)
            draw_seg(app, camera_space(app, &pts[i]),
                     camera_space(app, &pts[(i + 1) & 3]), COL_DIM);
    }
#endif
    (void)sc;

#ifdef WF_HOST_TRACE
fprintf(stderr, "    shadow done\n"); fflush(stderr);
#endif
#ifndef WF_BENCH_NOBOXES
    for (i = 0; i < NUM_BOXES; i++) {
#ifdef WF_HOST_TRACE
fprintf(stderr, "    box %d/%d (%d,%d) a=(%d,%d,%d) b=(%d,%d,%d)\n", (int)i, NUM_BOXES,
wf_boxes[i].a, wf_boxes[i].b, js[wf_boxes[i].a].v[0], js[wf_boxes[i].a].v[1], js[wf_boxes[i].a].v[2],
js[wf_boxes[i].b].v[0], js[wf_boxes[i].b].v[1], js[wf_boxes[i].b].v[2]); fflush(stderr);
#endif
        draw_box(app, &js[wf_boxes[i].a], &js[wf_boxes[i].b], wf_boxes[i].hw, col);
    }
#endif

#ifndef WF_BENCH_NOCYL
    for (i = 0; i < NUM_CYLINES; i++)
        draw_seg(app, camera_space(app, &js[wf_lines[i].a]),
                 camera_space(app, &js[wf_lines[i].b]), fi->col_sub);
#endif
}

static void draw_sparks(app_t *app)
{
    int i;
    for (i = 0; i < MAX_SPARKS; i++) {
        const spark_t *sp = &app->sparks[i];
        vec3 a, b, t;
        if (!sp->used)
            continue;
        a = camera_space(app, &sp->p);
        vmul(&t, (vec3 *)&sp->v, FIXC(2.5));
        vadd(&b, &sp->p, &t);
        b = camera_space(app, &b);
        draw_seg(app, a, b, sp->life > 5 ? sp->col : COL_WIRE3);
    }
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */
/* ------------------------------------------------------------------ */

static void draw_bigtext_centered(app_t *app, int y)
{
    static const uint8_t grad_norm[7] = { COL_FLASH, COL_WIRE6, COL_WIRE6, COL_WIRE5, COL_WIRE5, COL_WIRE4, COL_WIRE4 };
    static const uint8_t grad_ko[7]   = { COL_FLASH, COL_HAZ, COL_HAZ, COL_SUN, COL_SUN, COL_YEL, COL_YEL };
    int len = 0, sc;
    const uint8_t *grad;

    if (app->bigtext_life <= 0 || !app->bigtext[0])
        return;
    while (app->bigtext[len])
        len++;
    sc = app->bigtext[0] == 'K' ? 3 : 2;
    grad = app->bigtext[0] == 'K' ? grad_ko : grad_norm;
    gfx_bigtext(CX - len * 6 * sc / 2, y, app->bigtext, sc, grad, COL_BG);
}

#define TEXT_W(str) ((int)(sizeof(str) - 1) * 6)

static void draw_title_ui(app_t *app)
{
    static const uint8_t grad[7] = { COL_WIRE6, COL_WIRE6, COL_WIRE5, COL_WIRE5, COL_WIRE4, COL_WIRE4, COL_WIRE3 };
    static const char title[] = "WIREFIGHT";
    static const char press[] = "PRESS A OR START TO FIGHT";
    static const char ctrl[]  = "D-PAD MOVE  A JAB  B KICK  DOWN GUARD";

    draw_bigtext_centered(app, 96);

    gfx_bigtext(CX - 9 * 6 * 2 / 2, 8, title, 2, grad, COL_BG);
    if ((app->t / 15) % 2 == 0)
        gfx_text(CX - TEXT_W(press) / 2, 158, press, COL_WIRE6);
    gfx_text(CX - TEXT_W(ctrl) / 2, 168, ctrl, COL_WIRE4);
}

static void draw_hud(app_t *app)
{
    const int bw = 118;
    int side;

    for (side = 0; side < 2; side++) {
        fixed hp = side ? app->p2o.hp : app->p1o.hp;
        int x0 = side ? WF_VIEW_W - 8 - bw : 8;
        int fill;
        if (hp < 0)
            hp = 0;
        fill = fix2int(((bw - 2) * hp) / 100);
        gfx_rectb(x0, 8, bw, 7, COL_WIRE4);
        if (!side) {
            gfx_rect(x0 + 1, 9, fill, 5, COL_WIRE5);
            gfx_text(x0 + 2, 17, "1P", COL_WIRE4);
        } else {
            gfx_rect(x0 + bw - 1 - fill, 9, fill, 5, COL_SUN);
            gfx_text(x0 + bw - 10, 17, "2P", COL_WIRE4);
        }
    }
    /* timer */
    {
        int secs = fix2int(app->timer / FPS);
        char tb[3];
        if (secs < 0)
            secs = 0;
        tb[0] = (char)('0' + (secs / 10) % 10);
        tb[1] = (char)('0' + secs % 10);
        tb[2] = 0;
        gfx_text(CX - 6, 8, tb, COL_WIRE6);
    }

    draw_bigtext_centered(app, 52);

    if (app->t < 150) {
        static const char s[] = "DOWN GUARD   A JAB   B KICK";
        gfx_text(CX - TEXT_W(s) / 2, 100, s,
                 (app->t / 12) % 2 ? COL_WIRE6 : COL_WIRE4);
    }
}

static void draw_result_ui(app_t *app)
{
    static const char s[] = "A OR START : REMATCH";
    uint8_t col = COL_WIRE5;
    int len = 0;

    gfx_rect(CX - 74, 60, 148, 60, COL_BG);
    gfx_rectb(CX - 74, 60, 148, 60, COL_WIRE4);

    while (app->result_msg[len])
        len++;
    /* HAZ if "WIN" in msg */
    {
        int i;
        for (i = 0; i + 2 < len; i++)
            if (app->result_msg[i] == 'W' && app->result_msg[i + 1] == 'I' &&
                app->result_msg[i + 2] == 'N')
                col = COL_HAZ;
    }
    gfx_text(CX - len * 3, 72, app->result_msg, col);
    if (app->t > 25 && (app->t / 15) % 2 == 0)
        gfx_text(CX - TEXT_W(s) / 2, 96, s, COL_WIRE5);
}

/* ------------------------------------------------------------------ */
/* debug status strip (test builds only)                               */
/* ------------------------------------------------------------------ */

#ifdef WF_TESTBUILD

#define STRIP_Y0 206
#define STRIP_DUP 80

static void strip_px(int x, int y, int bit)
{
    /* bit 1 -> white (7), bit 0 -> bg (0); draw twice for redundancy */
    gfx_pset_abs(x, y, bit ? COL_FLASH : COL_BG);
    gfx_pset_abs(x + STRIP_DUP, y, bit ? COL_FLASH : COL_BG);
}

static void strip_byte(int row, int colbyte, uint32_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        strip_px(colbyte * 8 + i, STRIP_Y0 + row, (v >> (7 - i)) & 1);
}

static void draw_status_strip(app_t *app)
{
    int btxt = 0, sec;
    if (app->bigtext[0]) {
        btxt = app->bigtext[0] == 'K' ? 2 :
               app->bigtext[0] == 'F' ? 1 : 3;
    }
    sec = fix2int(app->timer / FPS);
    if (app->state != GS_PLAY)
        sec = 0;
    if (sec < 0) sec = 0;

    /* row 0: magic + state + t */
    strip_byte(0, 0, 0xAB);
    strip_byte(0, 1, 0xCD);
    strip_byte(0, 2, (uint32_t)app->state);
    strip_byte(0, 3, app->t & 0xFF);
    /* row 1: positions (Q12 >> 4 as unsigned 16) */
    {
        uint32_t x1 = (uint32_t)((fix2int(app->p1o.x << 12)) & 0xFFFF);
        uint32_t x2 = (uint32_t)((fix2int(app->p2o.x << 12)) & 0xFFFF);
        strip_byte(1, 0, (x1 >> 8) & 0xFF);
        strip_byte(1, 1, x1 & 0xFF);
        strip_byte(1, 2, (x2 >> 8) & 0xFF);
        strip_byte(1, 3, x2 & 0xFF);
    }
    /* row 2: hp + bigtext + timer */
    strip_byte(2, 0, (uint32_t)(fix2int(app->p1o.hp) & 0xFF));
    strip_byte(2, 1, (uint32_t)(fix2int(app->p2o.hp) & 0xFF));
    strip_byte(2, 2, (uint32_t)btxt);
    strip_byte(2, 3, (uint32_t)(sec & 0xFF));
    /* row 3: cam mode + states + parity */
    strip_byte(3, 0, (uint32_t)app->cam_mode);
    strip_byte(3, 1, (uint32_t)app->p1o.state);
    strip_byte(3, 2, (uint32_t)app->p2o.state);
    strip_byte(3, 3, (uint32_t)((app->p1o.motion << 4) | app->p2o.motion));
}

#endif /* WF_TESTBUILD */

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */

static void draw(app_t *app)
{
    setup_view(app);
#ifdef WF_HOST_TRACE
fprintf(stderr, "  setup_view done\n"); fflush(stderr);
#endif
#ifndef WF_BENCH_NODRAW
#ifndef WF_BENCH_NOSTAGE
    draw_stage(app);
#endif
#ifdef WF_HOST_TRACE
fprintf(stderr, "  draw_stage done\n"); fflush(stderr);
#endif
#ifndef WF_BENCH_NOFIGHT
    draw_fighter(app, &app->p1o);
#ifdef WF_HOST_TRACE
fprintf(stderr, "  draw_fighter p1 done\n"); fflush(stderr);
#endif
    draw_fighter(app, &app->p2o);
#ifdef WF_HOST_TRACE
fprintf(stderr, "  draw_fighter p2 done\n"); fflush(stderr);
#endif
    draw_sparks(app);
#endif
#ifdef WF_HOST_TRACE
fprintf(stderr, "  draw_sparks done\n"); fflush(stderr);
#endif
#ifndef WF_BENCH_NOUI
    switch (app->state) {
    case GS_PLAY:
        draw_hud(app);
        break;
    case GS_TITLE:
        draw_title_ui(app);
        break;
    default:
        draw_result_ui(app);
        break;
    }
#endif
#if defined(WF_TESTBUILD) && !defined(WF_BENCH_NOSTRIP)
    draw_status_strip(app);
#endif
#endif /* WF_BENCH_NODRAW */
}

void wf_run(void)
{
    app_t *app = &G;
    unsigned last = 0;

    /* fixed 30 fps logic via vblank accumulator */
    {
        int i;
        for (i = 0; i < (int)sizeof(G); i++)
            ((char *)&G)[i] = 0;
    }
    app->bgm_track = -1;
    gfx_srand(WF_RNG_SEED);
    to_title(app);
    Mars_SetPalette(wf_palette_rgb);

    for (;;) {
        /* wait the required number of vblanks for ~30 logic Hz */
        {
            static fixed acc = 0;
            int need;
            acc += ((fixed)mars_refresh_hz << 16) / FPS;
            need = acc >> 16;
            acc &= 0xFFFF;
            if (need < 1)
                need = 1;
            while ((int)(mars_vblank_count - last) < need);
            last = mars_vblank_count;
        }

        app->pad_prev = app->pad;
        app->pad = Mars_ReadController(0);

        update(app);
        update_camera(app);

#if defined(WF_TESTBUILD) || defined(WF_HOST)
        dbg_reset_stats();
        dbg_mark_vb();                    /* COMM2: vblanks spent waiting/updating */
#endif
        Mars_ClearScreen(COL_BG);
        draw(app);
#if defined(WF_TESTBUILD) || defined(WF_HOST)
        dbg_report_stats();
        dbg_mark_vb2();                   /* COMM4: vblanks spent drawing+flipping */
#endif
        /* MUST wait for the pending swap (which executes at the next vblank):
         * without the wait, the next iteration's clear+draw start on the bank
         * that is still about to become the front buffer, so the swap lands
         * mid-clear/mid-draw and smears stale content across the screen. */
        Mars_FlipFrameBuffers(1);
    }
}
