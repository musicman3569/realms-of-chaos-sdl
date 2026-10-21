/* RXROSSF.C: Ross, the castle's boss (castleb, level 7). The ROSS* animators in RXCASTLE.C
 * each call their FROSS* worker here.
 *
 * The fight is in a room at the map's right end. Ross waits above its ceiling until the view
 * has scrolled to the end, crashes through it in a shower of stones, then lands on the right
 * and alternates: throwing knives from the right, a sword lunge from the left, and jumping
 * across (GenericVar[0] keeps the stage of the routine). Flags bit 0 is the side he heads for:
 * set, the right. His images (RossJumpID[0] facing left, [1] in the air, [2] facing right, [3]
 * lunging) are mostly set straight into ImageID, so his box stays what OBJIMAGE last gave it,
 * and his sword (type 0x18, ROSSSWORD) follows whichever is shown. */
#include "behave.h"

/* The room's left wall: the player can't go left of it once the fight starts. */
static void keep_player_in(void)
{
    const uint16_t wall = (uint16_t)(G.lv->cols * 16 - 0x139);
    if (G.you->x < wall) {
        G.you->x = wall;
        G.you->xspeed = 0;
    }
}

static bool shows(const obj_t *o, int img)
{
    return o->image == G.img[img];
}

static uint8_t ross_max_hits(void)
{
    return boss_max_hits(0);
}

/* A ROSSSHAKE: an invisible object shaking the view (spawned as a type 0x18). */
static void spawn_shake(void)
{
    obj_t *s = spawn_obj(0x18, LIST_MAIN, NULL, G.org_x, G.org_y);
    s->animator = A_ROSSSHAKE;
    s->gv[0] = 0;
    s->counter = 0;
}

static void spawn_sword(int img, uint16_t x, uint16_t y)
{
    spawn_obj(0x18, LIST_MAIN, G.img[IMG_ROSSWORD + img], x, y);
}

/* RXROSSF.C:11 FROSSWAIT: wait for the view to reach the map's right end; then the view stops
 * following the player sideways (a dead zone the view's width), the boss's hit points show
 * (BOSSHITPOINTS) and he crashes in (ROSSCRASH, OF_RATE at xSpeed 800: 7 runs a second). */
int ross_wait(obj_t *o)
{
    if ((uint16_t)(G.lv->cols * 16 - 0x141) != G.org_x)
        return 0;
    o->flags |= 1 | OF_RATE;
    o->xspeed = 800;
    o->animator = A_ROSSCRASH;
    o->counter = 0;
    o->gv[0] = 0;
    o->gv[1] = 0;
    G.window_xsize = 0x140;
    G.window_ysize = 0x8C;
    set_scroll_bounds(0x8C, 0x140);
    spawn_obj(0x34, LIST_MAIN, G.img[IMG_BOSSHP + 10], (uint16_t)(G.org_x + 0x89),
              (uint16_t)(G.org_y + 7));
    play_sample(0x3C, NULL);
    return 0;
}

/* RXROSSF.C:33 FROSSCRASH: each run shakes the view and drops a stone from the ceiling (type
 * 0x1a, PiecesID[0], somewhere over x 0x186-0x1ad, drifting away from x 0x19a the faster the
 * farther out); after 0x9c4 ticks Ross falls through, sword first. */
int ross_crash(obj_t *o)
{
    spawn_shake();
    obj_t *p = spawn_obj(0x1A, LIST_MAIN, G.img[IMG_PIECES], (uint16_t)(frandom(0x28) + 0x186),
                         0x50);
    if (p->x < 0x19B)
        p->flags &= 0xFE;
    else
        p->flags |= 1;
    p->xspeed = (int16_t)(iabs16((int16_t)(p->x - 0x19A)) << 8);
    if (o->counter > 0x9C4) {
        o->flags &= ~OF_RATE;
        o->animator = A_GENERICBEGINFALL;
        obj_image(o, G.img[IMG_ROSSJUMP + 1]);
        spawn_sword(0, (uint16_t)(o->x - 0x51), (uint16_t)(o->y + 0x11));
        play_sample(0x9E, NULL);
    }
    return 0;
}

/* RXROSSF.C:62 FROSSPIECE: a falling stone (GENERICUP / GENERICFALL call it for priority
 * 0x3b, and it's the animator once landed): it drifts sideways at its rate; on the first
 * landing it bounces up again twice as fast sideways, on the second it goes. */
int ross_piece(obj_t *o)
{
    keep_player_in();
    if (carry(&o->xpart, o->xspeed)) {
        if (o->flags & 1)
            o->x++;
        else
            o->x--;
    }
    if (o->gv[5] & 1) {
        if (o->gv[0] == 0) {
            o->gv[0]++;
            o->gv[4] = 0x5A;
            o->xspeed = (int16_t)(o->xspeed << 1);
            o->yspeed = (int16_t)(-2000 - (int)frandom(8000));
            o->animator = A_GENERICBEGINJUMP;
        } else {
            obj_kill(o);
        }
    }
    return 0;
}

/* RXROSSF.C:92 FROSSJUMP: Ross in the air (GENERICUP / GENERICFALL, priority 0x3c): unless
 * he's falling in at the start (GenericVar[0] 0), he moves sideways 22000 / 65536 of a pixel a
 * tick towards his side. GENERICDOWN lands him on ROSSATTACK. */
void ross_jump(obj_t *o)
{
    keep_player_in();
    if (o->flags & OF_HIT)
        enemy_hit(o, ross_max_hits());
    if (o->gv[0] != 0 && carry(&o->xpart, 22000)) {
        if (o->flags & 1)
            o->x++;
        else
            o->x--;
    }
}

/* RXROSSF.C:119 FROSSATTACK: on the ground. Just landed (still RossJumpID[1]), he stands at
 * his side's place (x 0xb9 facing right, 0x188 facing left) and the view shakes. After 200
 * ticks: on his second visit to a side or 1 time in 4 he jumps across (GenericVar[4] 0xbe
 * ticks of lift at -26000); otherwise he lunges from the left (ROSSLUNGE) or throws knives
 * from the right (ROSSTHROW). */
int ross_attack(obj_t *o)
{
    keep_player_in();
    hit_player(o);
    if (o->flags & OF_HIT)
        enemy_hit(o, ross_max_hits());
    if (shows(o, IMG_ROSSJUMP + 1)) {
        o->counter = 0;
        if (!(o->flags & 1)) {
            o->image = G.img[IMG_ROSSJUMP + 2];
            o->x = 0xB9;
        } else {
            o->image = G.img[IMG_ROSSJUMP];
            o->x = 0x188;
        }
        spawn_shake();
    }
    if (o->counter <= 200)
        return 0;
    if (o->gv[0] < 2 && frandom(4) != 1) {
        if (!(o->flags & 1)) {
            o->image = G.img[IMG_ROSSJUMP + 3];
            o->animator = A_ROSSLUNGE;
            o->counter = 0;
            spawn_sword(1, (uint16_t)(o->x + 0x74), (uint16_t)(o->y + 0x33));
        } else {
            obj_image(o, G.img[IMG_ROSSTHR1]);
            o->animator = A_ROSSTHROW;
            o->counter = 0;
            o->y = (uint16_t)(o->y - 8);
        }
        return 0;
    }
    o->image = G.img[IMG_ROSSJUMP + 1];
    if (!(o->flags & 1)) {
        o->flags |= 1;
        spawn_sword(0, (uint16_t)(o->x - 0x51), (uint16_t)(o->y + 0x11));
    } else {
        o->flags &= 0xFFFE;
    }
    o->animator = A_GENERICBEGINJUMP;
    o->gv[0] = 1;
    o->gv[4] = 0xBE;
    o->yspeed = -26000;
    return 0;
}

/* RXROSSF.C:193 FROSSTHROW: on the right, by Counter: 100 the arm back, 200 up, 600 a knife
 * (type 0x19, falling at a random rate as it flies left) and, until five are thrown, back to
 * 0x65 (so from the second on, 200 jumps to 500); 800 the arm down, 900 the sword drawn and
 * ROSSATTACK, which must then jump (GenericVar[0] 2). A hit while throwing cuts it short. */
int ross_throw(obj_t *o)
{
    keep_player_in();
    hit_player(o);
    switch (o->counter) {
    case 100:
        o->image = G.img[IMG_ROSSTHR1 + 1];
        break;
    case 200:
        if (o->gv[0] == 2)
            o->counter = 500;
        o->image = G.img[IMG_ROSSTHR2];
        break;
    case 600: {
        play_sample(0x80, o);
        o->image = G.img[IMG_ROSSTHR2 + 1];
        obj_t *k = spawn_obj(0x19, LIST_MAIN, G.img[IMG_ROSSKNIF], (uint16_t)(o->x - 0x28),
                             (uint16_t)(o->y + 0x26));
        k->yspeed = (int16_t)(frandom(22000) + 2000);
        o->gv[0] = 2;
        if (++o->gv[1] < 5)
            o->counter = 0x65;
        break;
    }
    case 800:
        o->image = G.img[IMG_ROSSTHR1];
        break;
    case 900:
        o->gv[1] = 0;
        o->image = G.img[IMG_ROSSJUMP];
        o->animator = A_ROSSATTACK;
        o->counter = 0;
        spawn_sword(0, (uint16_t)(o->x - 0x59), (uint16_t)(o->y + 0x28));
        o->y = (uint16_t)(o->y + 8);
        break;
    default:
        break;
    }
    if (o->flags & OF_HIT) {
        enemy_hit(o, ross_max_hits());
        if (o->gv[1] != 0) {
            o->gv[0] = 2;
            o->gv[1] = 5;
            o->counter = 799;
        }
    }
    return 0;
}

/* RXROSSF.C:254 FROSSLUNGE: from the left, every 100 ticks a stage (GenericVar[0]): 1 he
 * crouches (RossJumpID[2]; 1 time in 4 he thinks better of it: ROSSATTACK), 2 he thrusts
 * (RossJumpID[3], the sword out), 3 he runs right (30000 a tick) past x 0x10b and jumps back.
 * He can't be hurt while running. */
int ross_lunge(obj_t *o)
{
    keep_player_in();
    if (o->flags & OF_HIT) {
        if (o->gv[0] < 3)
            enemy_hit(o, ross_max_hits());
        else
            o->flags &= 0xFD;
    }
    hit_player(o);
    if (o->counter <= 100)
        return 0;
    switch (o->gv[0]) {
    case 1:
        o->image = G.img[IMG_ROSSJUMP + 2];
        o->counter = 0;
        o->gv[0]++;
        if (frandom(4) == 1)
            o->animator = A_ROSSATTACK;
        break;
    case 2:
        o->gv[0]++;
        o->image = G.img[IMG_ROSSJUMP + 3];
        spawn_sword(1, (uint16_t)(o->x + 0x57), (uint16_t)(o->y + 0x2A));
        break;
    case 3:
        if (carry(&o->xpart, 30000) && ++o->x > 0x10B) {
            o->animator = A_GENERICBEGINJUMP;
            o->gv[4] = 0x22;
            o->yspeed = -26000;
        }
        break;
    default:
        break;
    }
    return 0;
}

/* RXROSSF.C:303 FROSSSWORD: Ross's sword (type 0x18, priority 0x3d) hurts, held where his
 * image has it: drawn back (RossJumpID[0]), raised in a jump ([1]) or thrust out ([3]). With
 * any other image it goes. */
int ross_sword(obj_t *o)
{
    hit_player(o);
    const obj_t *b = G.boss;
    uint16_t x, y;
    if (shows(b, IMG_ROSSJUMP)) {
        x = (uint16_t)(b->x - 0x59);
        y = (uint16_t)(b->y + 0x28);
    } else if (shows(b, IMG_ROSSJUMP + 1)) {
        o->image = G.img[IMG_ROSSWORD];
        x = (uint16_t)(b->x - 0x55);
        y = (uint16_t)(b->y + 0x0B);
    } else if (shows(b, IMG_ROSSJUMP + 3)) {
        x = (uint16_t)(b->x + 0x57);
        y = (uint16_t)(b->y + 0x2A);
    } else {
        obj_kill(o);
        return 0;
    }
    o->x = b->x == 0x4B ? 0x4B : x;
    o->y = y;
    return 0;
}

/* RXROSSF.C:331 FROSSSHAKE: the view jumps up a pixel at tick 20 and back at 40, twice, with
 * the camera let go meanwhile (_CenterOff -1, a dead zone the whole view's height). Then the
 * usual zone comes back on levels 17 and (for a player climbing or in the air) 15, the other
 * bosses' levels that shake. */
int ross_shake(obj_t *o)
{
    if (o->counter == 0x14) {
        G.center_off = -1;
        G.window_ysize = 200;
        scroll_up(1);
        set_scroll_bounds(200, G.level != 0x11 && G.level != 0x13 ? 0x140 : G.window_xsize);
    } else if (o->counter == 0x28) {
        if (G.org_y < (uint16_t)(G.lv->rows * 16 - 200))
            scroll_down(1);
        if (++o->gv[0] == 2) {
            G.center_off = 0;
            const uint16_t a = G.you->animator;
            if (G.level == 0x11 ||
                (G.level == 0x0F && (a == A_YOUCLIMB || a == A_YOUJUMP || a == A_YOUFALL))) {
                set_scroll_bounds(0x8C, G.window_xsize);
                G.window_ysize = 0x8C;
                G.center_off = 'V';
            }
            obj_kill(o);
        } else {
            o->counter = 0;
        }
    }
    return 0;
}

/* RXROSSF.C:364 FROSSKNIFE: a thrown knife (type 0x19, OF_RATE at 26000) spins left a pixel a
 * run and falls at its own rate; it hurts on the runs it doesn't drop. */
int ross_knife(obj_t *o)
{
    next_frame(o, IMG_ROSSKNIF);
    o->x--;
    if (carry(&o->ypart, o->yspeed))
        o->y++;
    else
        hit_player(o);
    return 0;
}

/* RXROSSF.C:381 FROSSDIE (ENEMYHIT gives it to Ross): he flickers (hidden for 4 ticks of every
 * 10, and for good after 150 bursts) while an explosion bursts over him every 10 ticks; after
 * 200 of them, ROSSCLEAR. The original keeps the hidden image at DS:50d2. */
int ross_die(obj_t *o)
{
    static const sprite_t *saved;
    keep_player_in();
    ++o->gv[0];
    if (o->gv[0] == 1) {
        saved = o->image;
        o->image = NULL;
    } else if (o->gv[0] == 5 && o->gv[1] < 0x96) {
        o->image = saved;
    } else if (o->gv[0] == 10) {
        o->gv[0] = 0;
        const uint16_t x = (uint16_t)(o->x + frandom((uint16_t)(o->xsize + 10)) - 0x14);
        const uint16_t y = (uint16_t)(o->y + frandom((uint16_t)(o->ysize - 0x14)));
        spawn_obj(0x0D, LIST_MAIN, G.img[IMG_EXPLODE], x, y);
        if (++o->gv[1] > 200) {
            o->animator = A_ROSSCLEAR;
            o->counter = 0;
        }
    }
    return 0;
}

/* RXROSSF.C:408 FROSSCLEAR: 1000 ticks later the level is won (_quitflag 3), if the player is
 * alive. It reads HitPoints[0], the female's, whichever character plays (as MAINTAINBLOCK). */
int ross_clear(obj_t *o)
{
    keep_player_in();
    if (o->counter > 1000 && Inv.hit_points[0] != 0)
        G.quitflag = 3;
    return 0;
}
