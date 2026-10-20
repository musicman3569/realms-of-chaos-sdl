/* RXCAVE.C: cave objects (and the forest's stakes, which share their code). */
#include "behave.h"

/* RXCAVE.C:96 DOCAVESPIKES: spikes (stakes in the forest) on a 0x520-tick cycle. Frame 0
 * is fully up, frame 4 down. At the start of the cycle (on screen) they're up and sink
 * through frames 1-4, 16 ticks apart; from tick 0x2fc they rise again. SPIKECHECK tests the
 * player while they're up (1: ticks 0-0x1f and from 0x31c) and half-way up (0). */
int do_cave_spikes(obj_t *o)
{
    const int img = G.level < 4 ? IMG_STAKE1 : IMG_CAVESPIK;
    switch (o->counter) {
    case 1:
        if (obj_visible(o)) {
            obj_image(o, G.img[img]);
            play_sample(G.level < 4 ? 0x63 : 0x65, o);
        }
        break;
    case 0x10: case 0x31C: obj_image(o, G.img[img + 1]); break;
    case 0x20: case 0x30C: obj_image(o, G.img[img + 2]); break;
    case 0x30: obj_image(o, G.img[img + 3]); break;
    case 0x40: obj_image(o, G.img[img + 4]); break;
    case 0x2FC:
        obj_image(o, G.img[img + 3]);
        play_sample(G.level < 4 ? 0x63 : 0x66, o);
        break;
    case 0x32C: obj_image(o, G.img[img]); break;
    case 0x520: o->counter = 0; break;
    default: break;
    }
    if (o->counter < 0x20 || o->counter > 0x31B)
        spike_check(o, 1);
    else if (o->counter >= 0x30C)
        spike_check(o, 0);
    return 0;
}
