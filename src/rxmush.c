/* RXMUSH.C: the mushroom levels' spore holes, the chains to climb, and the crusher's sliding
 * room (level 15). */
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

/* The crusher (mushromb, level 15): a shaft 30 tiles wide whose room slides from side to side.
 * The camera object (type 8, CRUSHERCENTER) swings between x 0 and 0x80 and the view follows
 * it; the walls (type 0x40, priority 10), the platforms (0x14) and the chains (0x15, 0x16) are
 * pinned to the view (MAINTAINSTUFF), and anything a wall pushes into the shaft's rock is
 * crushed. */

/* The player may be pushed right / left by a wall: not into rock, nor (on level 15) past the
 * room's other wall. */
static bool may_push_right(void)
{
    const obj_t *you = G.you;
    if (!can_go_right(you))
        return false;
    return G.level == 0x0F ? (uint16_t)(you->x + 0x0E) < (uint16_t)(G.org_x + 0x130)
                           : (uint16_t)(you->x + 0x0E) < (uint16_t)(G.lv->cols * 16 - 1);
}

static bool may_push_left(void)
{
    const obj_t *you = G.you;
    if (!can_go_left(you))
        return false;
    return G.level == 0x0F ? (uint16_t)(G.org_x + 0x12) < you->x : you->x >= 0x19;
}

/* RXMUSH.C:191 MOVESTUFF: as the room slides a pixel, whatever is in the air (GenericVar[5]
 * bit 0 clear) slides with it, unless it would leave the room (or, the player, while
 * climbing or dead). */
static void move_stuff(const obj_t *cam, obj_t *p)
{
    if (p->gv[5] & 1)
        return;
    obj_t *you = G.you;
    if (cam->xdir != 0 && can_go_right(p) && (uint16_t)(p->x + p->xsize) < (uint16_t)(G.org_x + 0x130)) {
        if (p != you)
            p->x++;
        else if (may_push_right() && !(G.game_flags & 1) && you->animator != A_NULLANIMATOR)
            player_right(1, you);
    } else if (cam->xdir == 0 && can_go_left(p) && (uint16_t)(G.org_x + 0x12) < p->x) {
        if (p != you)
            p->x--;
        else if (may_push_left() && !(G.game_flags & 1) && you->animator != A_NULLANIMATOR)
            player_left(1, you);
    }
}

/* RXMUSH.C:226 MAINTAINSTUFF: the walls stand at the view's left edge (GenericVar[0] 0) or
 * 0x130 / 0x138 right of it, the platforms at 0x108 (solid, carrying the player), the chains
 * at 0x120. */
static void maintain_stuff(obj_t *o)
{
    if (o->priority == 10) {
        o->x = o->gv[0] == 0   ? G.org_x
               : o->gv[0] == 1 ? (uint16_t)(G.org_x + 0x130)
                               : (uint16_t)(G.org_x + 0x138);
    } else if (o->priority == 0x14) {
        o->x = (uint16_t)(G.org_x + 0x108);
        o->gv[5] |= 4 | 8;
        maintain_platform(o);
        if (obj_overlap(G.you, o) && !(o->gv[5] & 0x20))
            maintain_block(o);
        o->gv[5] &= 0x60;
    } else {
        o->x = (uint16_t)(G.org_x + 0x120);
        if (o->priority == 0x15)
            do_chain(o);
    }
}

static bool pinned(const obj_t *o)
{
    return o->priority == 10 || o->priority == 0x14 || o->priority == 0x15 || o->priority == 0x16;
}

/* RXMUSH.C:71 CRUSHERCENTER: the camera. Its rate grows by 5 a tick to 0x1000; at each carry
 * it moves a pixel towards its end (GenericVar[0] 0) and carries what's in the air along, or
 * waits (GenericVar[0] counting to 0x20) and turns, with a thud and the rumble (sample 0x95,
 * its handle in GenericVar[4]). At either end the view shakes. The view follows it sideways
 * a pixel a tick, and the player up and down every 4 ticks like SCREENCENTER. Then every
 * object in the main list is kept in the room: pushed a pixel off a wall it overlaps, and the
 * player crushed (FYOUCRUSH) if the rock (or the other wall) leaves no room. */
int crusher_center(obj_t *o)
{
    obj_t *you = G.you;
    o->xspeed = (int16_t)(o->xspeed + 5);
    if (o->xspeed > 0x1000)
        o->xspeed = 0x1000;
    if (carry(&o->xpart, o->xspeed)) {
        if (o->gv[0] == 0) {
            if (o->xdir == 0)
                o->x--;
            else
                o->x++;
            if (o->x == (o->xdir == 0 ? 0 : 0x80)) {
                sound_link_kill(o->gv[4]);
                play_sample(0xA3, o);
                obj_t *s = spawn_obj(0x18, LIST_FORE, NULL, G.org_x, G.org_y);
                s->animator = A_ROSSSHAKE;
                s->gv[0] = 0;
                s->counter = 0;
                o->gv[0]++;
            }
            obj_t *next;
            for (obj_t *p = G.list[LIST_MAIN]; p; p = next) {
                next = p->next;
                if (p->image && !pinned(p))
                    move_stuff(o, p);
            }
        } else if (++o->gv[0] == 0x20) {
            o->xdir = !o->xdir;
            o->gv[0] = 0;
            o->xspeed = 0;
            play_sample(0x97, o);
            if (o->gv[4] != 0 && !sound_link_active(o->gv[4]))
                o->gv[4] = 0;
            if (o->gv[4] == 0)
                o->gv[4] = (int16_t)play_sample(0x95, o);
        }
    }
    if (carry(&o->ypart, 0x4000) && G.center_off > 0) {
        if (you->y < (uint16_t)(G.org_y + 0x4B) && G.org_y != 0) {
            scroll_up(1);
        } else if ((uint16_t)(G.org_y + G.center_off) < you->y &&
                   G.org_y < (uint16_t)(G.lv->rows * 16 - 200)) {
            scroll_down(1);
        } else if (G.window_ysize != 50) {
            set_scroll_bounds(0x32, G.window_xsize);
            G.window_ysize = 50;
        }
    }
    if (o->x < G.org_x)
        scroll_left(1);
    else if (G.org_x < o->x)
        scroll_right(1);
    obj_t *next;
    for (obj_t *p = G.list[LIST_MAIN]; p; p = next) {
        next = p->next;
        if (pinned(p)) {
            maintain_stuff(p);
            continue;
        }
        if (p->priority == 0x28)
            continue;
        if (p->x < (uint16_t)(G.org_x + 0x12)) {
            if (p != you)
                p->x++;
            else if (may_push_right()) {
                if (!(G.game_flags & 1))
                    player_right(1, you);
            } else {
                you_crush(you);
            }
        } else if ((uint16_t)(G.org_x + 0x130) < (uint16_t)(p->x + p->xsize)) {
            if (p != you)
                p->x--;
            else if (may_push_left()) {
                if (!(G.game_flags & 1))
                    player_left(1, you);
            } else {
                you_crush(you);
            }
        }
    }
    for (obj_t *p = G.list[LIST_FORE]; p; p = next) {
        next = p->next;
        if (p->priority == 10)
            maintain_stuff(p);
    }
    return 0;
}
