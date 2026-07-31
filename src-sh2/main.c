/*
 * WIREFIGHT 32X - SH2 entry points
 */
#include "marsl.h"
#include "wf.h"
#include "sound.h"

/* runs on the primary SH2 (cold start from crt0.s) */
int main(void)
{
    Mars_Init();
    Mars_InitVideo();
    snd_init();
    wf_run();
    return 0;
}

/* runs on the secondary SH2 */
void secondary(void)
{
    snd_slave();
}
