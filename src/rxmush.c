/* RXMUSH.C: the mushroom levels' spore holes, and the chains to climb. */
#include "behave.h"

/* RXMUSH.C:11 DOSPOREHOLE: an invisible vent (type 0x47, OF_RATE) on a 14-run cycle: it
 * puffs up (MushSporID[2], [1], [0]), spits a spore at the third run (type 0x48: thrown up and
 * to the right, then straight up, then to the left), closes again and vanishes from the
 * sixth. */
int do_spore_hole(obj_t *o)
{
    switch (++o->gv[0]) {
    case 1: case 5:
        o->image = G.img[IMG_MUSHSPOR + 2];
        break;
    case 2: case 4:
        o->image = G.img[IMG_MUSHSPOR + 1];
        break;
    case 3: {
        o->image = G.img[IMG_MUSHSPOR];
        play_sample(0x61, o);
        obj_t *s = spawn_obj(0x48, LIST_MAIN, G.img[IMG_GARGFIRE], (uint16_t)(o->x + 3),
                             (uint16_t)(o->y - 5));
        if (++o->gv[1] == 3) {
            o->gv[1] = 0;
            s->xdir = 0;
        } else {
            s->xdir = 1;
        }
        s->xspeed = o->gv[1] == 0 || o->gv[1] == 2 ? 8000 : 0;
        break;
    }
    case 6:
        o->image = NULL;
        break;
    case 0x0E:
        o->gv[0] = 0;
        break;
    default:
        break;
    }
    return 0;
}

/* RXMUSH.C:39 DOSPORE: a spore (or a statue bomb's flaming blob), priority 0x39. GENERICUP /
 * GENERICFALL call it in flight: it blinks, hurts the player it touches, drifts sideways at
 * xSpeed, and bursts against a wall, the map's edge, the player or the player's weapon;
 * landing (Animator DOSPORE) bursts it too. */
int do_spore(obj_t *o)
{
    if (G.you && obj_collide(o, G.you))
        hit_player(o);
    if (o->blinks == 0) {
        o->blink_color = 0x1D;
        obj_blink(o, 4, 2, 2);
    }
    const bool landed = o->animator == A_DOSPORE;
    if (carry(&o->xpart, o->xspeed) || landed) {
        can_move(o);
        if (o->xdir != 0 && can_go_right(o) && room_right(o))
            o->x++;
        else if (o->xdir == 0 && can_go_left(o) && room_left(o))
            o->x--;
        else
            o->up_offset = 0x0B;
        if (landed)
            o->up_offset = 0x0B;
    }
    if (o->up_offset > 10 || landed || (G.you && obj_collide(o, G.you)) || (o->flags & OF_HIT)) {
        play_sample(0x62, NULL);
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], (uint16_t)(o->x - 3),
                  (uint16_t)(o->y - 3))->gv[0] = 0;
        obj_kill(o);
    }
    return 0;
}

/* RXMUSH.C:258 DOCHAIN: a chain to climb (type 0x42, priority 0x15). On goblin2 (level 23)
 * it hangs from the goblin boss's platform and moves with it. Up or down while touching it
 * grabs it (unless recovering from a hit, attacking, or impaled / dead): the player starts
 * climbing as on a vine (FYOUBEGINCLIMB, but with _Vine_x 0 and the player 3 pixels off the
 * chain's x), and GenericVar[5] bit 0x20 marks the chain held. While held it pulls the player
 * a pixel a run back to its place (turning on the chain moves the player 5 pixels); leaving
 * the chain lets go, and the player falls unless another chain is held. */
int do_chain(obj_t *o)
{
    obj_t *you = G.you;
    if (G.level == 0x17 && G.boss)
        o->x = (uint16_t)(G.boss->x + 0x10);
    if (!(o->gv[5] & 0x20)) {
        if (!key(K_UP) && !key(K_DOWN))
            return 0;
        if (!obj_overlap(you, o) || (G.game_flags & 8) || (G.game_flags & 2) ||
            you->animator == A_YOUBEGINIMPALE || you->animator == A_NULLANIMATOR)
            return 0;
        set_scroll_bounds(0x8C, G.window_xsize);
        G.window_ysize = 0x8C;
        G.center_off = 'V';
        o->gv[5] |= 0x20;
        you->gv[5] &= ~4 & ~1;
        you->frame_no = 0;
        you->xspeed = 0;
        you->yspeed = 0;
        you->friction = 199;
        you->animator = A_YOUCLIMB;
        you->anim_inc = 1;
        you->max_frames = 6;
        G.vine_x = 0;
        G.game_flags |= 1;
        you->frame_count_max = (G.game_flags & 4) ? 5 : 6;
        you->frame_count = you->frame_count_max;
        obj_image(you, G.img[G.you_climb]);
        you->x = you->xdir == 0 ? (uint16_t)(o->x + 3) : (uint16_t)(o->x - 3);
        return 0;
    }
    if (!obj_overlap(you, o)) {
        bool riding_another = false;
        for (const obj_t *p = G.list[LIST_MAIN]; p; p = p->next)
            if (p->priority == 0x15 && p != o && (p->gv[5] & 0x20))
                riding_another = true;
        if (riding_another)
            o->gv[5] &= 0xDF;
        else
            you->animator = A_YOUBEGINFALL;
    }
    if (you->animator != A_YOUCLIMB) {
        o->gv[5] &= 0xDF;
        return 0;
    }
    const uint16_t x = you->xdir == 0 ? (uint16_t)(o->x + 3) : (uint16_t)(o->x - 3);
    if (you->x < x)
        player_right(1, you);
    else if (you->x > x)
        player_left(1, you);
    return 0;
}

/* RXMUSH.C:333 DOCHAINEND: the chain's last link (type 0x43) follows the boss's platform. */
int do_chain_end(obj_t *o)
{
    if (G.boss)
        o->x = (uint16_t)(G.boss->x + 0x10);
    return 0;
}
