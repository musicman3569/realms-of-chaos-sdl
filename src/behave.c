/* The ported behaviours, by Animator value (an offset in code segment 1000). engine.c looks
 * an object's Animator up here; values that aren't listed do nothing yet. */
#include "behave.h"

#include <stdio.h>
#include <stdlib.h>

typedef int (*animator_fn)(obj_t *);
typedef struct {
    uint16_t offset;
    animator_fn fn;
} animator_t;

/* Sorted by offset (checked on first use). */
static const animator_t table[] = {
    {0x0da4 /* DOCAVESPIKES */, do_cave_spikes},
    {A_DOLEAF, do_leaf},
    {A_DOBIRD, do_bird},
    {A_BANDITWALK, bandit_walk},
    {A_DOBAT, do_bat},
    {A_WOLFRUN, wolf_run},
    {A_DOARROWSIGN, do_arrow_sign},
    {A_GENERICBEGINJUMP, generic_begin_jump},
    {A_GENERICJUMP, generic_jump},
    {A_GENERICUP, generic_up},
    {A_GENERICBEGINFALL, generic_begin_fall},
    {A_GENERICFALL, generic_fall},
    {A_DOPROJECTILE, do_projectile},
    {A_DOCONTAINER, do_container},
    {A_SCREENCENTER, screen_center},
    {A_FLOATINGPLATFORM, floating_platform},
    {A_CRUMBLINGPLATFORM, crumbling_platform},
    {A_CRUMBLEPLATSHAKE, crumble_plat_shake},
    {A_CRUMBLEPLATFALL, crumble_plat_fall},
    {A_DOPULLEY, do_pulley},
    {A_PULLYPLATWAIT, pully_plat_wait},
    {A_PULLYPLATMOVE, pully_plat_move},
    {A_PULLYPLATRETURN, pully_plat_return},
    {A_PULLYPLATDIP, pully_plat_dip},
    {A_YOUBEGINRUN, you_begin_run},
    {A_YOURUN, you_run},
    {A_YOUBEGINJUMP, you_begin_jump},
    {A_YOUJUMP, you_jump},
    {A_YOUUP, you_up},
    {A_YOUBEGINFALL, you_begin_fall},
    {A_YOUFALL, you_fall},
    {A_YOUHORIZ, you_horiz},
    {A_YOUBEGINCROUCH, you_begin_crouch},
    {A_YOUBEGINSTUN, you_begin_stun},
    {A_YOUCROUCH, you_crouch},
    {A_YOUBEGINCLIMB, you_begin_climb},
    {A_YOUCLIMB, you_climb},
    {A_YOUCLIMBMOVE, you_climb_move},
    {A_YOUBEGINSCRAMBLE, you_begin_scramble},
    {A_YOUSCRAMBLE, you_scramble},
    {A_NULLANIMATOR, null_animator},
};

const animator_t *behaviours(int *count)
{
    static bool checked;
    const int n = (int)(sizeof table / sizeof table[0]);
    if (!checked) {
        for (int i = 1; i < n; i++) {
            if (table[i - 1].offset >= table[i].offset) {
                fprintf(stderr, "roc: behaviour table out of order at %04x\n", table[i].offset);
                abort();
            }
        }
        checked = true;
    }
    *count = n;
    return table;
}
