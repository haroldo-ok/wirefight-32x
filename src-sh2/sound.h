/*
 * WIREFIGHT 32X - sound (PWM, mixed on the slave SH2)
 *
 * The original Pyxel game uses 8 sound slots:
 *   1 = whoosh (swing), 2 = jab hit, 3 = guard, 4 = round bell,
 *   5 = K.O. boom, 6 = win fanfare, 7 = kick hit
 * plus looping title / fight BGM. We reproduce them with a small
 * integer synthesizer running on the secondary SH2.
 */
#ifndef WF_SOUND_H
#define WF_SOUND_H

enum {
    SND_WHOOSH = 1,
    SND_JAB    = 2,
    SND_GUARD  = 3,
    SND_BELL   = 4,
    SND_KO     = 5,
    SND_WIN    = 6,
    SND_KICK   = 7,
};

enum {
    SND_SONG_NONE  = 0,
    SND_SONG_TITLE = 1,
    SND_SONG_FIGHT = 2,
};

void snd_init(void);          /* called once on the master before the slave is started */
void snd_bgm(int song);       /* switch the background music */
void snd_play(int ch, int id);/* trigger a one-shot effect on slot ch (1..3) */
void snd_slave(void);         /* mixing loop; runs on the slave SH2, never returns */

#endif /* WF_SOUND_H */
