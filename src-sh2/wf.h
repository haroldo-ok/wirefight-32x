/*
 * WIREFIGHT 32X - game core
 */
#ifndef WF_GAME_H
#define WF_GAME_H

void wf_run(void);        /* main game loop, never returns */
void wf_sound_tick(void); /* sound command pump, called by the slave cpu */

#endif
