/* Object behaviours (Animator functions), one file per original module: rxgenerl.c
 * (RXGENERL.C), rxforest.c (RXFOREST.C / RXFRSTF.C), rxplat.c (RXPLAT.C), rxcave.c (RXCAVE.C),
 * rxgems.c (RXGEMS.C), rxgrab.c (RXGRABF.C), rxyou.c (RXYOU.C / RXYOUF.C).
 * behave.c maps Animator values to them. */
#ifndef ROC_BEHAVE_H
#define ROC_BEHAVE_H

#include "engine.h"

/* Add a speed to a 16-bit fraction; true when it carries out of 16 bits. A negative speed
 * added this way carries unless the fraction borrows, so for upward motion "no carry" is the
 * step. */
static inline bool carry(uint16_t *part, int16_t speed)
{
    const uint16_t old = *part;
    *part = (uint16_t)(*part + speed);
    return *part < old;
}

static inline int16_t iabs16(int16_t v)
{
    return v < 0 ? (int16_t)-v : v;
}

/* The "room to move" tests the walkers share: not past the map's (or, on the autoscrolling
 * level 15, the view's) left / right edge. */
bool room_left(const obj_t *o);
bool room_right(const obj_t *o);

/* rxgenerl.c */
int null_animator(obj_t *o);
int do_arrow_sign(obj_t *o);
int generic_begin_fall(obj_t *o);
int generic_fall(obj_t *o);
int generic_begin_jump(obj_t *o);
int generic_jump(obj_t *o);
int generic_up(obj_t *o);
int do_projectile(obj_t *o);
int do_container(obj_t *o);
int enemy_hit_pause(obj_t *o);
int do_explosion(obj_t *o);
int exploder_spawner(obj_t *o);
int splash_spawner(obj_t *o);
int do_collectable(obj_t *o);

/* engine.c (RXTIMERF.C) */
int off_killer(obj_t *o);

/* rxgems.c */
int do_flying_shit(obj_t *o);

/* rxforest.c */
int do_leaf(obj_t *o);
int do_bird(obj_t *o);
int do_bat(obj_t *o);
int bandit_walk(obj_t *o);
int wolf_run(obj_t *o);

/* rxplat.c */
int floating_platform(obj_t *o);
int crumbling_platform(obj_t *o);
int crumble_plat_shake(obj_t *o);
int crumble_plat_fall(obj_t *o);
int do_pulley(obj_t *o);
int pully_plat_wait(obj_t *o);
int pully_plat_move(obj_t *o);
int pully_plat_return(obj_t *o);
int pully_plat_dip(obj_t *o);
void maintain_platform(obj_t *o);

/* rxcave.c */
int do_cave_spikes(obj_t *o);

/* rxyou.c */
int you_begin_run(obj_t *o);
int you_run(obj_t *o);
int you_begin_jump(obj_t *o);
int you_jump(obj_t *o);
int you_up(obj_t *o);
int you_begin_fall(obj_t *o);
int you_fall(obj_t *o);
int you_horiz(obj_t *o);
int you_begin_crouch(obj_t *o);
int you_begin_stun(obj_t *o);
int you_crouch(obj_t *o);
int you_begin_climb(obj_t *o);
int you_climb(obj_t *o);
int you_climb_move(obj_t *o);
int you_begin_scramble(obj_t *o);
int you_scramble(obj_t *o);
int you_begin_shoot(obj_t *o);
int you_shoot(obj_t *o);
int do_fireball(obj_t *o);
int you_begin_whip(obj_t *o);
int you_whip(obj_t *o);
int do_whip(obj_t *o);
int you_hit(obj_t *o);
int you_begin_impale(obj_t *o);
int you_die(obj_t *o);
int you_crush(obj_t *o);
int you_drown(obj_t *o);
int drop_gem_spawner(obj_t *o);
int do_hit_points(obj_t *o);
int do_gem_number(obj_t *o);
int do_blood(obj_t *o);
int screen_center(obj_t *o);

#endif
