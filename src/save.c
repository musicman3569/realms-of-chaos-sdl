/* RXSAVEF.C: ROC.CFG, roc.sco and saved games (see save.h). */
#include "save.h"

#include "engine.h"
#include "game.h"
#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern int8_t dragon_move[4][180]; /* rxswamp.c: _DragonMove */

static char dir[1024];
static bool enabled;

void save_set_dir(const char *d)
{
    enabled = d != NULL;
    snprintf(dir, sizeof dir, "%s", d ? d : "");
}

/* APOGEECD: a file in the game's folder. */
static void path_of(char *out, size_t n, const char *name)
{
    const size_t len = strlen(dir);
    const bool sep = len && dir[len - 1] != '/' && dir[len - 1] != '\\';
    snprintf(out, n, "%s%s%s", dir, sep ? "/" : "", name);
}

static bool read_file(const char *name, void *buf, size_t size)
{
    if (!enabled)
        return false;
    char path[1100];
    path_of(path, sizeof path, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    const bool ok = fread(buf, 1, size, f) == size;
    fclose(f);
    return ok;
}

static bool write_file(const char *name, const void *buf, size_t size)
{
    if (!enabled)
        return false;
    char path[1100];
    path_of(path, sizeof path, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(buf, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    return ok;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

/* ------------------------------------------------------------------------------------------ */
/* ROC.CFG (CONFIGstruct, 70 bytes)                                                            */

enum { CFG_SIZE = 0x46 };
static uint8_t cfg[CFG_SIZE];    /* the file as read: what the port doesn't use is kept */
static bool cfg_read;

bool config_load(void)
{
    if (!read_file("ROC.CFG", cfg, CFG_SIZE))
        return false;
    cfg_read = true;
    for (int i = 0; i < 8; i++)
        Sess.key_trans[i] = rd16(cfg + i * 2);
    Sess.status_on = cfg[44];
    Sess.message_on = cfg[45];
    Sess.blood_on = cfg[47];
    memcpy(Sess.blood_pass, cfg + 48, 20);
    Sess.blood_pass[19] = 0;
    Sess.gamma = cfg[68] <= 0x10 ? cfg[68] : 0;
    Sess.sound_card = (int16_t)rd16(cfg + 32);
    Sess.sound_voices = (int16_t)rd16(cfg + 34);
    Sess.sound_volume = (int16_t)rd16(cfg + 36);
    Sess.music_card = (int16_t)rd16(cfg + 38);
    Sess.music_volume = (int16_t)rd16(cfg + 42);
    memcpy(Sess.my_button, cfg + 16, 4);
    for (int i = 0; i < 6; i++)
        Sess.joy_cal[i] = rd16(cfg + 20 + i * 2);
    Sess.joystick_on = cfg[46];
    return true;
}

void config_save(void)
{
    if (!cfg_read) {
        /* what the port doesn't set: the MIDI port at 330h */
        memset(cfg, 0, sizeof cfg);
        wr16(cfg + 40, 0x330);
    }
    /* (the joystick as read: STARTUPCHECKLIST turns _JoystickOn off for a missing joystick and
     * then saves that; the port keeps the setting for when it's plugged in again) */
    memcpy(cfg + 16, Sess.my_button, 4);
    for (int i = 0; i < 6; i++)
        wr16(cfg + 20 + i * 2, Sess.joy_cal[i]);
    cfg[46] = Sess.joystick_on;
    wr16(cfg + 32, (uint16_t)Sess.sound_card);
    wr16(cfg + 34, (uint16_t)Sess.sound_voices);
    wr16(cfg + 36, (uint16_t)Sess.sound_volume);
    wr16(cfg + 38, (uint16_t)Sess.music_card);
    wr16(cfg + 42, (uint16_t)Sess.music_volume);
    for (int i = 0; i < 8; i++)
        wr16(cfg + i * 2, Sess.key_trans[i]);
    cfg[44] = Sess.status_on;
    cfg[45] = Sess.message_on;
    cfg[47] = Sess.blood_on;
    memset(cfg + 48, 0, 20);
    memcpy(cfg + 48, Sess.blood_pass, strnlen(Sess.blood_pass, 19));
    cfg[68] = Sess.gamma;
    if (write_file("ROC.CFG", cfg, CFG_SIZE))
        cfg_read = true;
}

/* ------------------------------------------------------------------------------------------ */
/* ROCP.CFG (the port's)                                                                       */

enhance_t Enhance = {.crt = true, .fullscreen = true, .controller = true, .stereo_music = true};

static const struct {
    const char *name;
    bool *on;
} enhance_names[] = {
    {"crt", &Enhance.crt},
    {"fullscreen", &Enhance.fullscreen},
    {"controller", &Enhance.controller},
    {"stereo_music", &Enhance.stereo_music},
    {"title_scream", &Enhance.title_scream},
};
enum { ENHANCE_NAMES = sizeof enhance_names / sizeof enhance_names[0] };

void enhance_load(void)
{
    if (!enabled)
        return;
    char path[1100];
    path_of(path, sizeof path, "ROCP.CFG");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (int i = 0; i < ENHANCE_NAMES; i++)
            if (!strcmp(line, enhance_names[i].name))
                *enhance_names[i].on = eq[1] == '1';
    }
    fclose(f);
}

void enhance_save(void)
{
    char text[512];
    size_t n = 0;
    for (int i = 0; i < ENHANCE_NAMES; i++)
        n += (size_t)snprintf(text + n, sizeof text - n, "%s=%d\n", enhance_names[i].name,
                              *enhance_names[i].on);
    write_file("ROCP.CFG", text, n);
}

/* ------------------------------------------------------------------------------------------ */
/* roc.sco (HSCORE, 594 bytes)                                                                 */

enum { HS_SIZE = 0x252 };

static void hs_names(char (*names)[24], const uint8_t *p)
{
    for (int i = 0; i < 3; i++) {
        memcpy(names[i], p + i * 24, 24);
        names[i][23] = 0;
    }
}

static void hs_unpack(hscore_t *h, const uint8_t *b)
{
    memset(h, 0, sizeof *h);
    hs_names(h->score_name, b + 0);
    for (int i = 0; i < 3; i++) {
        h->score[i] = rd32(b + 72 + i * 4);
        h->time_hour[i] = rd16(b + 156 + i * 2);
        h->time_mins[i] = rd16(b + 162 + i * 2);
        h->time_secs[i] = rd16(b + 168 + i * 2);
        h->enemy[i] = rd16(b + 246 + i * 2);
        h->statue[i] = rd16(b + 324 + i * 2);
        h->gems[i] = rd16(b + 402 + i * 2);
        h->secret[i] = rd16(b + 480 + i * 2);
    }
    hs_names(h->time_name, b + 84);
    hs_names(h->enemy_name, b + 174);
    hs_names(h->statue_name, b + 252);
    hs_names(h->gems_name, b + 330);
    hs_names(h->secret_name, b + 408);
    for (int i = 0; i < 27; i++) {
        h->best_minute[i] = rd16(b + 486 + i * 2);
        h->best_second[i] = rd16(b + 540 + i * 2);
    }
}

static void hs_pack(const hscore_t *h, uint8_t *b)
{
    memset(b, 0, HS_SIZE);
    for (int i = 0; i < 3; i++) {
        memcpy(b + i * 24, h->score_name[i], 24);
        memcpy(b + 84 + i * 24, h->time_name[i], 24);
        memcpy(b + 174 + i * 24, h->enemy_name[i], 24);
        memcpy(b + 252 + i * 24, h->statue_name[i], 24);
        memcpy(b + 330 + i * 24, h->gems_name[i], 24);
        memcpy(b + 408 + i * 24, h->secret_name[i], 24);
        wr32(b + 72 + i * 4, h->score[i]);
        wr16(b + 156 + i * 2, h->time_hour[i]);
        wr16(b + 162 + i * 2, h->time_mins[i]);
        wr16(b + 168 + i * 2, h->time_secs[i]);
        wr16(b + 246 + i * 2, h->enemy[i]);
        wr16(b + 324 + i * 2, h->statue[i]);
        wr16(b + 402 + i * 2, h->gems[i]);
        wr16(b + 480 + i * 2, h->secret[i]);
    }
    for (int i = 0; i < 27; i++) {
        wr16(b + 486 + i * 2, h->best_minute[i]);
        wr16(b + 540 + i * 2, h->best_second[i]);
    }
}

void hs_read(hscore_t *h)
{
    uint8_t b[HS_SIZE];
    if (read_file("roc.sco", b, HS_SIZE)) {
        hs_unpack(h, b);
        return;
    }
    size_t len = 0;
    uint8_t *d = Arc ? arc_load(Arc, "rocscore.txt", &len) : NULL;
    memset(b, 0, sizeof b);
    if (d)
        memcpy(b, d, len < HS_SIZE ? len : HS_SIZE);
    free(d);
    hs_unpack(h, b);
}

void hs_write(const hscore_t *h)
{
    uint8_t b[HS_SIZE];
    hs_pack(h, b);
    write_file("roc.sco", b, HS_SIZE);
}

/* ------------------------------------------------------------------------------------------ */
/* Saved games                                                                                 */

enum { SAVE_VERSION = 1, HEADER = 5 + 1 + 20 + 1 };
static const char ident[5] = "ROCP";
static const char orig_ident[5] = "ROCX"; /* the original's, with version 'd' */

static void slot_name(char *out, size_t n, int slot)
{
    if (slot == RESTORE_TEMP)
        snprintf(out, n, "ROCP.SGT");
    else
        snprintf(out, n, "ROCP.SG%c", '0' + slot - 1);
}

/* A growing buffer, written and read in little-endian order. */
typedef struct {
    uint8_t *p;
    size_t len, cap, pos;
    bool bad;
    bool original;               /* (a slot read from the original's ROC.SGn) */
} buf_t;

/* The animators' numbers in the original's saved games: the table at DS:059a (STOREOBJECTINFO
 * and MAKEOBJECT), every Animator a saved object can have, ending with OFFKILLER. */
enum { ANIM_TABLE = 0x059A, ANIM_MAX = 400, DGROUP_PARA = 0x3A77 };
static uint16_t anim_table[ANIM_MAX];
static int anim_count;

bool save_load_tables(const char *exe_path)
{
    anim_count = 0;
    FILE *f = fopen(exe_path, "rb");
    uint8_t mz[10];
    bool ok = f && fread(mz, 1, sizeof mz, f) == sizeof mz && mz[0] == 'M' && mz[1] == 'Z';
    if (ok)
        ok = fseek(f, (long)rd16(mz + 8) * 16 + DGROUP_PARA * 16L + ANIM_TABLE, SEEK_SET) == 0;
    while (ok && anim_count < ANIM_MAX) {
        uint8_t w[2];
        ok = fread(w, 1, 2, f) == 2;
        if (ok)
            anim_table[anim_count++] = rd16(w);
        if (ok && rd16(w) == A_OFFKILLER)
            break;
    }
    if (f)
        fclose(f);
    if (!ok || anim_count == ANIM_MAX)
        anim_count = 0;
    return anim_count > 0;
}

/* Which file a slot (1-8) is read from: the original's ROC.SGn when it's newer than the port's
 * ROCP.SGn or there's no ROCP.SGn, so a game saved last in either is the one loaded. Saving
 * always writes ROCP.SGn. The reader's ROC.SGT is never read (the original deletes it once
 * restored). */
static bool original_slot(int slot)
{
    if (!enabled || slot < 1 || slot > SAVE_SLOTS)
        return false;
    char name[16], path[1100];
    struct stat orig, port;
    snprintf(name, sizeof name, "ROC.SG%c", '0' + slot - 1);
    path_of(path, sizeof path, name);
    if (stat(path, &orig) != 0)
        return false;
    slot_name(name, sizeof name, slot);
    path_of(path, sizeof path, name);
    return stat(path, &port) != 0 || orig.st_mtime > port.st_mtime;
}

static void put(buf_t *b, const void *data, size_t n)
{
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 65536;
        while (cap < b->len + n)
            cap *= 2;
        uint8_t *p = realloc(b->p, cap);
        if (!p) {
            b->bad = true;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, data, n);
    b->len += n;
}

static void put8(buf_t *b, unsigned v)
{
    const uint8_t c = (uint8_t)v;
    put(b, &c, 1);
}

static void put16(buf_t *b, unsigned v)
{
    uint8_t c[2];
    wr16(c, v);
    put(b, c, 2);
}

static void put32(buf_t *b, uint32_t v)
{
    uint8_t c[4];
    wr32(c, v);
    put(b, c, 4);
}

static void get(buf_t *b, void *data, size_t n)
{
    if (b->pos + n > b->len) {
        b->bad = true;
        memset(data, 0, n);
        return;
    }
    memcpy(data, b->p + b->pos, n);
    b->pos += n;
}

static uint8_t get8(buf_t *b)
{
    uint8_t c;
    get(b, &c, 1);
    return c;
}

static uint16_t get16(buf_t *b)
{
    uint8_t c[2];
    get(b, c, 2);
    return rd16(c);
}

static uint32_t get32(buf_t *b)
{
    uint8_t c[4];
    get(b, c, 4);
    return rd32(c);
}

/* Every object, list by list in order, numbered from 0. */
static int obj_count(void)
{
    int n = 0;
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next)
            n++;
    return n;
}

static int obj_number(const obj_t *x)
{
    if (!x)
        return -1;
    int n = 0;
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next, n++)
            if (o == x)
                return n;
    return -1;
}

static int name_number(const char *name)
{
    for (int i = 0; name && G.lv && i < G.lv->object_count; i++)
        if (name == G.lv->objects[i].info)
            return i;
    return -1;
}

/* The state, in a fixed order: the inventory, the game's lasting state, the loop's totals and
 * this level's counts, G (the level's), the palettes, the maps, the dragon's delay lines and
 * the objects. W(x) is put16 or get16 and so on, so one list serves both ways. */
#define FIELDS(B8, B16, B32, BYTES)                                                              \
    B32(Inv.score); B16(Inv.gems); BYTES(Inv.max_hit_points, 2); BYTES(Inv.hit_points, 2);       \
    BYTES(Inv.weapon_status, 2); B8(Inv.flags);                                                  \
    B16(L.second_total); B16(L.minute_total); B16(L.hour_total); B16(L.gem_total);               \
    B16(L.enemy_total); B16(L.statue_total); B16(L.secret_total); B16(L.gem_final);              \
    B16(L.enemy_final); B16(L.statue_final); B16(L.secret_final); B16(G.gem_in_level);           \
    B16(G.secret_in_level); B16(G.statue_in_level); B8(L.return_level);                          \
    B16(G.org_x); B16(G.org_y); B32(G.seed); B8(G.diff_mode); B16(G.game_flags);                 \
    B8(G.lightning_flag); B8(G.next_level); B8(G.message_on); B8(G.you_invincible);             \
    B16(G.window_xsize); B8(G.window_ysize); B8(G.center_off); B8(G.wisp_walk);                  \
    B16(G.vine_x); B16(G.vine_y); B16(G.scroll_left); B16(G.scroll_right);                       \
    B16(G.scroll_upper); B16(G.scroll_lower); B8(G.blood_on); B8(G.status_on);                   \
    B16(G.hitagain); B8(G.free_hit); B8(G.hit_release); B8(G.whip_fc); B8(G.whip_fcm);           \
    B8(G.whip_fn); B8(G.whip_ai); B16(G.secret_found); B16(G.gem_found); B16(G.enemy_found);     \
    B16(G.statue_found); B16(G.enemy_in_level); B8(G.monster_counter); B8(G.monster_number);    \
    B8(G.max_monster_number); B8(G.monster_spawns); B16(G.timer_ticks); B16(G.splash_side);     \
    B32(G.old_inv.score); B16(G.old_inv.gems); BYTES(G.old_inv.max_hit_points, 2);              \
    BYTES(G.old_inv.hit_points, 2); B8(G.old_inv.flags); B16(G.old_game_flags); B8(G.gamma);    \
    B32(G.next_score_up); B32(G.old_next_score_up); B32(G.level_tics); B32(G.next_second);      \
    B8(G.timer_fix); B8(G.second_in_level); B8(G.minute_in_level); B16(G.parent_priority);      \
    B16(G.face_timer); B8(G.neck_count); B16(G.you_walk); B16(G.you_jump); B16(G.you_duck);     \
    B16(G.you_climb); B16(G.you_attack);                                                         \
    BYTES(G.virt_pal, 768); BYTES(G.level_pal, 768); BYTES(G.lv->palette, 768);                 \
    BYTES(dragon_move, sizeof dragon_move)

/* The port's own format: everything, pointers as object numbers. */
static bool save_port(int slot, const char *desc)
{
    buf_t b = {0};
    put(&b, ident, 5);
    put8(&b, SAVE_VERSION);
    char d[20] = {0};
    memcpy(d, desc, strnlen(desc, 19));
    put(&b, d, 20);
    put8(&b, G.level);

#define P8(x) put8(&b, (uint8_t)(x))
#define P16(x) put16(&b, (uint16_t)(x))
#define P32(x) put32(&b, (uint32_t)(x))
#define PBYTES(x, n) put(&b, (x), (n))
    FIELDS(P8, P16, P32, PBYTES);
    put8(&b, (uint8_t)G.lv->rows);
    put8(&b, (uint8_t)G.lv->cols);
    const int cells = G.lv->rows * G.lv->cols;
    for (int i = 0; i < cells; i++)
        put16(&b, G.lv->bg[i]);
    put(&b, G.lv->fg, (size_t)cells);

    put16(&b, (uint16_t)obj_count());
    for (int id = 0; id < LIST_COUNT; id++) {
        for (const obj_t *o = G.list[id]; o; o = o->next) {
            put8(&b, (uint8_t)id);
            put8(&b, (uint8_t)o->list);
            P16(o->priority); P16(o->blinks); P16(o->blink_ct); P16(o->draw_on);
            P16(o->draw_off); P16(o->blink_color); P16(game_image_index(o->image));
            P16(o->flags); P8(o->xdir); P8(o->ydir); P16(o->x); P16(o->y); P16(o->xpart);
            P16(o->ypart); P16(o->xspeed); P16(o->yspeed); P16(o->xsize); P16(o->ysize);
            P16(o->update); P16(o->counter); P16(o->animator); P16(o->old_animator);
            P16(name_number(o->name)); P8(o->friction); P8(o->frame_no); P8(o->max_frames);
            P8(o->anim_inc); P16(o->frame_count); P16(o->frame_count_max); P8(o->down_offset);
            P8(o->up_offset);
            for (int i = 0; i < 6; i++)
                P16(o->gv[i]);
            P8(o->surface_flags); P16(obj_number(o->child)); P16(obj_number(o->parent));
        }
    }
    P16(obj_number(G.you));
    P16(obj_number(G.boss));
    for (int i = 0; i < 5; i++)
        P16(obj_number(G.neck[i]));
    P16(obj_number(G.hit_pts));
#undef P8
#undef P16
#undef P32
#undef PBYTES

    char name[16];
    slot_name(name, sizeof name, slot);
    const bool ok = !b.bad && write_file(name, b.p, b.len);
    free(b.p);
    return ok;
}

/* An animator's number in the DS:059a table (no Animator counts as NULLANIMATOR, as INITOBJ and
 * SPAWNOBJ start every object with it), or -1 if the table hasn't it (STOREOBJECTINFO stops the
 * game with an error then). */
static int anim_number(uint16_t animator)
{
    if (animator == 0)
        animator = A_NULLANIMATOR;
    for (int i = 0; i < anim_count; i++)
        if (anim_table[i] == animator)
            return i;
    return -1;
}

/* The objects SAVEGAME leaves out of the main list (and of the off-screen list's objects whose
 * home is the main list): reflections and the raft's front piece. */
static bool not_saved(const obj_t *o)
{
    return o->animator == A_DOREFLECTION || (o->priority == 0x18 && o->animator == A_NULLANIMATOR);
}

/* RXSAVEF.C:273 STOREOBJECTINFO: one object (see restore_original). */
static bool put_object(buf_t *b, const obj_t *o, int list_num)
{
    const int animator = anim_number(o->animator), old_animator = anim_number(o->old_animator);
    if (animator < 0 || old_animator < 0) {
        fprintf(stderr, "roc: animator %04x isn't in the original's table\n",
                animator < 0 ? o->animator : o->old_animator);
        return false;
    }
    /* the image's _Img index; none (or the port's empty frame) is the first empty slot, as the
     * original finds a null ImageID */
    int image = game_image_index(o->image);
    for (int i = 0; image < 0 && i < IMG_COUNT; i++)
        if (!G.img[i])
            image = i;
    int global = 0;
    if (o == G.you)
        global = 1;
    else if (o == G.boss)
        global = 2;
    else if (o == G.hit_pts)
        global = 3;
    for (int i = 1; global == 0 && i < 5; i++)
        if (o == G.neck[i])
            global = 3 + i;
    put8(b, (unsigned)list_num);
    put8(b, (uint8_t)o->priority);
    put16(b, (unsigned)animator);
    put16(b, (unsigned)old_animator);
    put16(b, (unsigned)(image < 0 ? 0 : image));
    put8(b, (unsigned)global);
    put16(b, o->blinks);
    put16(b, o->blink_ct);
    put16(b, o->draw_on);
    put16(b, o->draw_off);
    put16(b, o->blink_color);
    put16(b, o->flags);
    put8(b, o->xdir);
    put8(b, o->ydir);
    put16(b, o->x);
    put16(b, o->y);
    put16(b, o->xpart);
    put16(b, o->ypart);
    put16(b, (uint16_t)o->xspeed);
    put16(b, (uint16_t)o->yspeed);
    put16(b, o->xsize);
    put16(b, o->ysize);
    put16(b, o->update);
    put16(b, o->counter);
    put8(b, o->friction);
    put8(b, o->frame_no);
    put8(b, o->max_frames);
    put8(b, (uint8_t)o->anim_inc);
    put16(b, o->frame_count);
    put16(b, o->frame_count_max);
    put8(b, o->down_offset);
    put8(b, o->up_offset);
    for (int k = 0; k < 6; k++)
        put16(b, (uint16_t)o->gv[k]);
    put8(b, o->surface_flags);
    return true;
}

/* RXSAVEF.C:78 SAVEGAME: the original's ROC.SGn (see restore_original), so the DOS game can load
 * a game the port saved. What the port keeps beyond it (the random seed, a wisp light's wisp,
 * the dragon's delay lines...) isn't in it, as in the original. */
static bool save_original(int slot, const char *desc)
{
    if (anim_count == 0)
        return false;
    buf_t b = {0};
    put(&b, orig_ident, 5);
    put8(&b, SAVE_NONE);
    char d[20] = {0};
    memcpy(d, desc, strnlen(desc, 19));
    put(&b, d, 20);
    put8(&b, G.level);
    int count = 0;
    for (int id = 0; id < LIST_COUNT; id++)
        for (const obj_t *o = G.list[id]; o; o = o->next)
            count += !(o->list == LIST_MAIN && not_saved(o));
    put16(&b, (unsigned)count);

    /* the maps, the cells' tiles numbered as TILEBUFFOPTI would have them (the lowest number
     * of a tile wins: the numbers above the compacted ones are left over) */
    uint16_t tile[512], number[512];
    animtile_opti_numbers(tile);
    for (int n = 0; n < 512; n++)
        number[n] = (uint16_t)n;
    for (int n = 511; n >= 0; n--)
        number[tile[n] & 0x1FF] = (uint16_t)n;
    const int cells = G.lv->rows * G.lv->cols;
    put8(&b, (unsigned)G.lv->rows);
    put8(&b, (unsigned)G.lv->cols);
    for (int i = 0; i < cells; i++)
        put16(&b, (G.lv->bg[i] & 0xFE00u) | number[G.lv->bg[i] & 0x1FF]);
    static const uint8_t past[6]; /* (memory past the BGMAP in the original) */
    put(&b, past, sizeof past);
    put16(&b, (unsigned)G.lv->cols);
    put(&b, G.lv->fg, (size_t)cells);

    uint8_t pal[768];
    host_get_palette(pal);
    put16(&b, G.org_x);
    put16(&b, G.org_y);
    put16(&b, G.vine_x);
    put16(&b, G.vine_y);
    put16(&b, (uint16_t)G.hitagain);
    put16(&b, G.monster_counter);
    put8(&b, G.monster_number);
    put8(&b, G.free_hit);
    put8(&b, Inv.flags);
    put(&b, Inv.hit_points, 2);
    put(&b, Inv.max_hit_points, 2);
    put(&b, G.old_inv.max_hit_points, 2);
    put(&b, Inv.weapon_status, 2);
    put8(&b, (uint8_t)G.lightning_flag);
    put8(&b, G.diff_mode);
    put8(&b, G.free_hit);
    put32(&b, Inv.score);
    put32(&b, G.old_inv.score);
    put32(&b, G.next_score_up);
    put32(&b, G.old_next_score_up);
    put16(&b, Inv.gems);
    put16(&b, G.old_inv.gems);
    put16(&b, G.window_xsize);
    put8(&b, G.window_ysize);
    put8(&b, (uint8_t)G.center_off);
    put8(&b, G.hit_release);
    put8(&b, (uint8_t)G.whip_fn);
    put8(&b, (uint8_t)G.whip_ai);
    put8(&b, (uint8_t)G.whip_fc);
    put8(&b, (uint8_t)G.whip_fcm);
    put8(&b, (uint8_t)G.looking);
    put(&b, pal, sizeof pal);
    put16(&b, G.game_flags);
    put16(&b, G.old_game_flags);
    put8(&b, L.quit_game);
    put8(&b, G.quitflag);
    put16(&b, G.gem_in_level);
    put16(&b, G.enemy_in_level);
    put16(&b, G.statue_in_level);
    put16(&b, G.secret_in_level);
    put16(&b, G.gem_found);
    put16(&b, L.gem_total);
    put16(&b, G.enemy_found);
    put16(&b, L.enemy_total);
    put16(&b, G.statue_found);
    put16(&b, L.statue_total);
    put16(&b, G.secret_found);
    put16(&b, L.secret_total);
    put16(&b, L.hour_total);
    put16(&b, L.minute_total);
    put16(&b, L.second_total);
    put16(&b, L.gem_total);
    put8(&b, G.minute_in_level);
    put8(&b, G.second_in_level);
    put16(&b, L.gem_final);
    put16(&b, L.enemy_final);
    put16(&b, L.statue_final);
    put16(&b, L.secret_final);
    put16(&b, G.timer_fix);
    put32(&b, G.level_tics);
    put32(&b, G.next_second);
    put8(&b, L.return_level);

    /* main, fore and slow lists, then the off-screen list by home list (3-5) */
    bool ok = true;
    for (int id = 0; id < LIST_OFF && ok; id++)
        for (const obj_t *o = G.list[id]; o && ok; o = o->next)
            if (!(id == LIST_MAIN && not_saved(o)))
                ok = put_object(&b, o, id);
    for (const obj_t *o = G.list[LIST_OFF]; o && ok; o = o->next)
        if (!(o->list == LIST_MAIN && not_saved(o)))
            ok = put_object(&b, o, LIST_OFF + o->list);

    char name[16];
    snprintf(name, sizeof name, "ROC.SG%c", '0' + slot - 1);
    ok = ok && !b.bad && write_file(name, b.p, b.len);
    free(b.p);
    return ok;
}

/* SAVEGAME: slots 1-8 are written in both formats, the original's ROC.SGn first, so the port's
 * ROCP.SGn is never the older (original_slot) and the port loads its own; the DOS game reads
 * ROC.SGn. The reader's save is the port's only. */
bool save_game(int slot, const char *desc)
{
    if (!G.lv)
        return false;
    if (slot >= 1 && slot <= SAVE_SLOTS && !save_original(slot, desc))
        fprintf(stderr, "roc: couldn't write ROC.SG%c in the original's format\n", '0' + slot - 1);
    return save_port(slot, desc);
}

/* Read a slot's file: the port's, or the original's (both start with the same header). */
static bool load_slot(int slot, buf_t *b)
{
    char name[16], path[1100];
    const bool original = original_slot(slot);
    if (original)
        snprintf(name, sizeof name, "ROC.SG%c", '0' + slot - 1);
    else
        slot_name(name, sizeof name, slot);
    path_of(path, sizeof path, name);
    memset(b, 0, sizeof *b);
    FILE *f = enabled ? fopen(path, "rb") : NULL;
    if (!f)
        return false;
    uint8_t chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        put(b, chunk, n);
    fclose(f);
    b->original = original;
    const bool ok = original ? !memcmp(b->p, orig_ident, 5) && b->p[5] == SAVE_NONE
                             : !memcmp(b->p, ident, 5) && b->p[5] == SAVE_VERSION;
    if (b->bad || b->len < HEADER || !ok) {
        free(b->p);
        b->p = NULL;
        return false;
    }
    return true;
}

bool save_describe(int slot, char desc[21])
{
    buf_t b;
    if (!load_slot(slot, &b))
        return false;
    memcpy(desc, b.p + 6, 20);
    desc[20] = 0;
    free(b.p);
    return true;
}

int save_level(int slot)
{
    buf_t b;
    if (!load_slot(slot, &b))
        return SAVE_NONE;
    const int level = b.p[HEADER - 1];
    free(b.p);
    return level;
}

void save_delete(int slot)
{
    char name[16], path[1100];
    slot_name(name, sizeof name, slot);
    path_of(path, sizeof path, name);
    if (enabled)
        remove(path);
}

/* An object's number, read back as the object. */
static obj_t *obj_ref(buf_t *b, obj_t **all, int count)
{
    const int n = (int16_t)get16(b);
    return all && n >= 0 && n < count ? all[n] : NULL;
}

/* RXSAVEF.C:371 RESTOREGAME and :515 MAKEOBJECT: a game the original saved. After the header,
 * the object count, the maps as they are in memory (BGMAP: Rows, Cols, the cells and 6 bytes
 * past them; FGMAP: Cols as a word, the cells), about 60 globals and the objects: main, fore and
 * slow lists, then the off-screen list (3-5: its home list), each as list, priority, the
 * animator's and the old animator's numbers in the DS:059a table, the _Img index of its image, a
 * byte for the globals pointing at it, OBJstruct from Blinks to Counter (no pointers) and
 * USERstruct from Friction to SurfaceFlags. SAVEGAME leaves out the reflections (MAKEOBJECT makes
 * them again) and objects of priority 0x18 with NULLANIMATOR, the raft's front piece (the raft
 * makes a new one: its GenericVar[2] is cleared). The objects go into their lists by priority
 * (ADDPLINKNODE), so equal priorities come back in reverse order, as in the original. */
static bool restore_original(buf_t *b)
{
    b->pos = HEADER;
    const int count = get16(b);
    const int rows = get8(b), cols = get8(b);
    if (rows != G.lv->rows || cols != G.lv->cols || anim_count == 0)
        return false;
    const int cells = rows * cols;
    uint8_t skip[6];
    /* the saved cells number the tiles as TILEBUFFOPTI left them */
    uint16_t tile[512];
    animtile_opti_numbers(tile);
    for (int i = 0; i < cells; i++) {
        const uint16_t v = get16(b);
        G.lv->bg[i] = (uint16_t)((v & 0xFE00) | tile[v & 0x1FF]);
    }
    get(b, skip, 6);
    get(b, skip, 2);
    get(b, G.lv->fg, (size_t)cells);

    G.org_x = get16(b);
    G.org_y = get16(b);
    G.vine_x = get16(b);
    G.vine_y = get16(b);
    G.hitagain = (int16_t)get16(b);
    G.monster_counter = (uint8_t)get16(b);
    G.monster_number = get8(b);
    G.free_hit = get8(b);
    const uint8_t inv_flags = get8(b); /* (bits 6 and 7 are whatever was on the stack) */
    get(b, Inv.hit_points, 2);
    get(b, Inv.max_hit_points, 2);
    get(b, G.old_inv.max_hit_points, 2);
    get(b, Inv.weapon_status, 2);
    G.lightning_flag = (int8_t)get8(b);
    G.diff_mode = get8(b);
    G.free_hit = get8(b);
    Inv.score = get32(b);
    G.old_inv.score = get32(b);
    G.next_score_up = get32(b);
    G.old_next_score_up = get32(b);
    Inv.gems = get16(b);
    G.old_inv.gems = get16(b);
    G.window_xsize = get16(b);
    G.window_ysize = get8(b);
    G.center_off = (int8_t)get8(b);
    G.hit_release = get8(b);
    G.whip_fn = (int8_t)get8(b);
    G.whip_ai = (int8_t)get8(b);
    G.whip_fc = (int8_t)get8(b);
    G.whip_fcm = (int8_t)get8(b);
    G.looking = (int8_t)get8(b);
    get(b, L.save_pal, sizeof L.save_pal); /* the palette shown (GETPALETTE) */
    G.game_flags = get16(b);
    G.old_game_flags = get16(b);
    get8(b); /* _QuitGame */
    get8(b); /* _quitflag */
    G.gem_in_level = get16(b);
    G.enemy_in_level = get16(b);
    G.statue_in_level = get16(b);
    G.secret_in_level = get16(b);
    G.gem_found = get16(b);
    L.gem_total = get16(b);
    G.enemy_found = get16(b);
    L.enemy_total = get16(b);
    G.statue_found = get16(b);
    L.statue_total = get16(b);
    G.secret_found = get16(b);
    L.secret_total = get16(b);
    L.hour_total = get16(b);
    L.minute_total = get16(b);
    L.second_total = get16(b);
    L.gem_total = get16(b); /* (_GemTotal twice) */
    G.minute_in_level = get8(b);
    G.second_in_level = get8(b);
    L.gem_final = get16(b);
    L.enemy_final = get16(b);
    L.statue_final = get16(b);
    L.secret_final = get16(b);
    G.timer_fix = (uint8_t)get16(b);
    G.level_tics = get32(b);
    G.next_second = get32(b);
    L.return_level = get8(b);
    Inv.flags = inv_flags & 0x3F;
    G.max_monster_number = G.diff_mode < 3 ? 1 : G.diff_mode == 3 ? 2 : 3;

    game_clear_objects();
    G.you = G.boss = G.hit_pts = NULL;
    memset(G.neck, 0, sizeof G.neck);
    for (int i = 0; i < count && !b->bad; i++) {
        const int list = get8(b), priority = get8(b);
        const unsigned animator = get16(b), old_animator = get16(b), image = get16(b);
        const int global = get8(b);
        obj_t *o = calloc(1, sizeof *o);
        if (!o) {
            b->bad = true;
            break;
        }
        o->blinks = get16(b);
        o->blink_ct = get16(b);
        o->draw_on = get16(b);
        o->draw_off = get16(b);
        o->blink_color = get16(b);
        o->flags = get16(b) | 8;
        o->xdir = get8(b);
        o->ydir = get8(b);
        o->x = get16(b);
        o->y = get16(b);
        o->xpart = get16(b);
        o->ypart = get16(b);
        o->xspeed = (int16_t)get16(b);
        o->yspeed = (int16_t)get16(b);
        o->xsize = get16(b);
        o->ysize = get16(b);
        o->update = get16(b);
        o->counter = get16(b);
        o->friction = get8(b);
        o->frame_no = get8(b);
        o->max_frames = get8(b);
        o->anim_inc = (int8_t)get8(b);
        o->frame_count = get16(b);
        o->frame_count_max = get16(b);
        o->down_offset = get8(b);
        o->up_offset = get8(b);
        for (int k = 0; k < 6; k++)
            o->gv[k] = (int16_t)get16(b);
        o->surface_flags = get8(b);
        if (b->bad || animator >= (unsigned)anim_count || old_animator >= (unsigned)anim_count) {
            free(o);
            b->bad = true;
            break;
        }
        /* _Img[n] as it is: an object without an image (a camera, a hidden wisp) was saved as
         * the first empty slot, which is empty again now */
        o->image = image < IMG_COUNT ? G.img[image] : NULL;
        o->animator = anim_table[animator];
        o->old_animator = anim_table[old_animator];
        switch (global) {
        case 1: G.you = o; break;
        case 2: G.boss = G.neck[0] = o; break;
        case 3: G.hit_pts = o; break;
        case 4: case 5: case 6: case 7: G.neck[global - 3] = o; break;
        default: break;
        }
        o->priority = (int16_t)priority;
        o->list = list < LIST_OFF ? list : list <= 5 ? list - LIST_OFF : LIST_MAIN;
        obj_link(o, list < LIST_OFF ? list : LIST_OFF);
        if (o->list == LIST_MAIN)
            reflection_check(o);
        if (o->animator == A_SWAMPRAFT)
            o->gv[2] = 0;
    }
    /* A wisp's lights kept the wisp in GenericVar[0] as a near pointer, which means nothing once
     * the objects are made again (nor did it in the original); the port gives each the nearest
     * wisp, or lets it go if there's none. */
    for (int id = 0; id < LIST_COUNT; id++) {
        for (obj_t *o = G.list[id]; o; o = o->next) {
            if (o->animator != A_DOWISP || (o->gv[1] != 1 && o->gv[1] != 2))
                continue;
            unsigned best = 0xFFFFFFFFu;
            for (int k = 0; k < LIST_COUNT; k++) {
                for (obj_t *w = G.list[k]; w; w = w->next) {
                    if (w->animator != A_DOWISP || w->gv[1] != 0)
                        continue;
                    const unsigned d = (unsigned)abs((int16_t)(w->x - o->x)) +
                                       (unsigned)abs((int16_t)(w->y - o->y));
                    if (d < best) {
                        best = d;
                        o->parent = w;
                    }
                }
            }
            if (!o->parent)
                o->animator = A_OFFKILLER;
        }
    }
    set_hero_sets();
    set_scroll_bounds(G.window_ysize, G.window_xsize);
    return !b->bad;
}

bool restore_game(int slot)
{
    buf_t b;
    if (!load_slot(slot, &b) || !G.lv)
        return false;
    b.pos = HEADER;
    if (b.p[HEADER - 1] != G.level) {
        free(b.p);
        return false;
    }
    if (b.original) {
        const bool ok = restore_original(&b);
        free(b.p);
        G.whip_hit_count = 0;
        G.quitflag = 0;
        G.menu_coming = -1;
        L.restore_x = G.org_x;
        L.restore_y = G.org_y;
        return ok;
    }

#define G8(x) ((x) = get8(&b))
#define G16(x) ((x) = get16(&b))
#define G32(x) ((x) = get32(&b))
#define GBYTES(x, n) get(&b, (x), (n))
    FIELDS(G8, G16, G32, GBYTES);
    const int rows = get8(&b), cols = get8(&b);
    if (rows != G.lv->rows || cols != G.lv->cols) {
        free(b.p);
        return false;
    }
    for (int i = 0; i < rows * cols; i++)
        G.lv->bg[i] = get16(&b);
    get(&b, G.lv->fg, (size_t)rows * cols);

    game_clear_objects();
    const int count = get16(&b);
    obj_t **all = calloc((size_t)count + 1, sizeof *all);
    for (int i = 0; all && i < count; i++)
        all[i] = calloc(1, sizeof **all);
    obj_t *tail[LIST_COUNT] = {0};
    for (int i = 0; all && all[i] && i < count && !b.bad; i++) {
        obj_t *o = all[i];
        const int id = get8(&b);
        o->list = get8(&b);
        G16(o->priority); G16(o->blinks); G16(o->blink_ct); G16(o->draw_on); G16(o->draw_off);
        G16(o->blink_color);
        o->image = game_image((int16_t)get16(&b));
        G16(o->flags); G8(o->xdir); G8(o->ydir); G16(o->x); G16(o->y); G16(o->xpart);
        G16(o->ypart); G16(o->xspeed); G16(o->yspeed); G16(o->xsize); G16(o->ysize);
        G16(o->update); G16(o->counter); G16(o->animator); G16(o->old_animator);
        const int name = (int16_t)get16(&b);
        o->name = name >= 0 && name < G.lv->object_count ? G.lv->objects[name].info : NULL;
        G8(o->friction); G8(o->frame_no); G8(o->max_frames); G8(o->anim_inc);
        G16(o->frame_count); G16(o->frame_count_max); G8(o->down_offset); G8(o->up_offset);
        for (int k = 0; k < 6; k++)
            G16(o->gv[k]);
        G8(o->surface_flags);
        o->child = obj_ref(&b, all, count);
        o->parent = obj_ref(&b, all, count);
        /* append, keeping the saved order */
        if (id < LIST_COUNT) {
            if (tail[id])
                tail[id]->next = o;
            else
                G.list[id] = o;
            tail[id] = o;
        } else {
            free(o);
            all[i] = NULL;
        }
    }
    G.you = obj_ref(&b, all, count);
    G.boss = obj_ref(&b, all, count);
    for (int i = 0; i < 5; i++)
        G.neck[i] = obj_ref(&b, all, count);
    G.hit_pts = obj_ref(&b, all, count);
#undef G8
#undef G16
#undef G32
#undef GBYTES
    /* objects that didn't make it into a list (a short file) */
    for (int i = 0; all && i < count; i++) {
        bool linked = false;
        for (int id = 0; id < LIST_COUNT && !linked; id++)
            for (const obj_t *o = G.list[id]; o && !linked; o = o->next)
                linked = o == all[i];
        if (!linked)
            free(all[i]);
    }
    free(all);
    const bool ok = !b.bad;
    free(b.p);
    G.whip_hit_count = 0;
    G.quitflag = 0;
    G.looking = 0;
    G.menu_coming = -1;
    L.restore_x = G.org_x;
    L.restore_y = G.org_y;
    memcpy(L.save_pal, G.lv->palette, sizeof L.save_pal);
    return ok;
}
