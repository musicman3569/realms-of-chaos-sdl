/* Object behaviours (Animator functions), one file per original module: rxgenerl.c
 * (RXGENERL.C), rxforest.c (RXFOREST.C / RXFRSTF.C), rxplat.c (RXPLAT.C), rxcave.c (RXCAVE.C),
 * rxgems.c (RXGEMS.C), rxgrab.c (RXGRABF.C), rxyou.c (RXYOU.C / RXYOUF.C), rxskel.c
 * (RXSKELF.C), rxgoblin.c (RXGOBLIN.C), rxswamp.c (RXSWAMP.C / RXSWAMPF.C), rxmush.c
 * (RXMUSH.C), rxcastle.c (RXCASTLE.C), rxdead.c (RXDEAD.C).
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
int do_gargoyle(obj_t *o);
int do_gargoyle_fire(obj_t *o);
int do_cave_hole(obj_t *o);
int do_cave_spider(obj_t *o);
int do_cat_skeleton(obj_t *o);

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
int do_square_plat(obj_t *o);
int do_falling_plat(obj_t *o);
int brik_shake(obj_t *o);
int do_container_bomb(obj_t *o);
int swamp_raft(obj_t *o);
int frog_plat(obj_t *o);

/* rxcave.c */
int do_cave_spikes(obj_t *o);
int do_cave_fall_spikes(obj_t *o);
int do_torch_puff(obj_t *o);
int do_drop(obj_t *o);

/* rxskel.c */
int do_boar(obj_t *o);
int do_boar_attack(obj_t *o);
int do_boar_spear(obj_t *o);

/* rxgoblin.c */
int goblin_stand(obj_t *o);
int goblin_run(obj_t *o);
int goblin_hit(obj_t *o);
int goblin_fist(obj_t *o);
void goblin_jump(obj_t *o);
int do_rock_monster(obj_t *o);
void rock_monster_fall(obj_t *o);

/* rxswamp.c */
int mud_man_walk(obj_t *o);
int do_mosquito(obj_t *o);
int do_frog(obj_t *o);
int frog_launch(obj_t *o);
void frog_jump(obj_t *o);
int frog_land(obj_t *o);

/* rxmush.c */
int do_spore_hole(obj_t *o);
int do_spore(obj_t *o);
int do_chain(obj_t *o);
int do_chain_end(obj_t *o);

/* rxcastle.c */
int do_knife_guard(obj_t *o);
int do_guard_knife(obj_t *o);
int do_bomb_guard(obj_t *o);
int do_bomb(obj_t *o);
int bomb_exploder(obj_t *o);
int do_back_stars(obj_t *o);

/* rxdead.c */
int do_wisp(obj_t *o);

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
int do_ambient_noise(obj_t *o);
int screen_center(obj_t *o);

#endif
