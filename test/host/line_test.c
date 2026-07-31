#include <stdint.h>
#include <stdio.h>
#include "wf_host_shim.h"
#include "../../src-sh2/gfx.h"
int main(void)
{
    Mars_InitVideo();
    fprintf(stderr, "start\n");
    gfx_line(165, 119, 164, 118, 7);
    fprintf(stderr, "line1 ok\n");
    gfx_line(168, 120, 167, 125, 7);
    fprintf(stderr, "line2 ok\n");
    /* fuzz: random segments, catch any that never return */
    for (int i = 0; i < 200000; i++) {
        int x1 = (i * 2654435761u) % 700 - 200;
        int y1 = (i * 2246822519u) % 600 - 200;
        int x2 = x1 + ((i * 3266489917u) % 800) - 400;
        int y2 = y1 + ((i * 668265263u)  % 500) - 250;
        gfx_line(x1, y1, x2, y2, 7);
    }
    fprintf(stderr, "fuzz ok\n");
    return 0;
}
