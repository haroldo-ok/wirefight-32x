/*
 * Host renderer: runs the exact game C code (wf.c + gfx.c + data tables)
 * against a software framebuffer to reproduce/debug the frame the SH2
 * draws, without the emulator.
 *
 * usage: host_render [frames] [out.ppm]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "wf_host_shim.h"
#include "../../src-sh2/gfx.h"

/* the game itself */
#include "../../src-sh2/wf.c"

int main(int argc, char **argv)
{
    app_t *app = &G;
    int frames = argc > 1 ? atoi(argv[1]) : 90;
    const char *out = argc > 2 ? argv[2] : "/tmp/host_frame.ppm";
    int f, i;

    Mars_InitVideo();

    for (i = 0; i < (int)sizeof(G); i++)
        ((char *)&G)[i] = 0;
    app->bgm_track = -1;
    gfx_srand(WF_RNG_SEED);
    to_title(app);
    Mars_SetPalette(wf_palette_rgb);

    for (f = 0; f < frames; f++) {
        app->pad_prev = app->pad;
        app->pad = 0;
        update(app);
        update_camera(app);
        dbg_reset_stats();
        Mars_ClearScreen(COL_BG);
        draw(app);
        if (f >= 0)
            fprintf(stderr, "f%d state=%d cam=%d lines=%d plots=%d maxc=%d\n",
                    f, app->state, app->cam_mode, wf_dbg_lines, wf_dbg_plots, wf_dbg_maxc);
        Mars_FlipFrameBuffers(0);
    }

    wf_host_dump_ppm(out);
    printf("wrote %s after %d frames (state=%d t=%d)\n", out, frames,
           app->state, app->t);
    return 0;
}
