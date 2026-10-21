/* The in-level windows (RXWINDF.C, and WINUP / SYSPIXELWRITE / FONTSTRLEN /
 * WINCLEARTEXTAREA / PUTWINPIC from the window assembly at 3f50). See ui.h. */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DS:1fb2: the font frame (RocFontID) for each character, -1 for none. */
static const int8_t font_index[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  37, 38, -1, 39, -1, 40, 43, 41, 42, 46, -1, 45, 44, 50, 51,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 47, -1, -1, 48, -1, 49,
    -1, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, -1, -1, -1, -1, -1,
    -1, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, -1, -1, -1, -1, -1,
};

/* _FontColorTable0..D (DS:0e52): the font's five pixel values in each colour, back to back
 * (the blitter's xlat can read past a table into the next). */
static const uint8_t font_colors[14 * 5] = {
    23, 72, 74, 76, 78, 23, 8,  10, 12, 14,  23, 73, 75, 77, 79,  23, 9,  11, 13, 15,
    23, 72, 74, 76, 78, 23, 8,  10, 12, 14,  23, 97, 98, 99, 100, 23, 30, 29, 28, 27,
    23, 48, 49, 50, 51, 23, 105, 106, 107, 108, 53, 53, 53, 53, 53, 1,  1,  1,  1,  1,
    23, 16, 17, 18, 19, 23, 72, 72, 72, 72,
};

enum { W_NONE, W_STATUS, W_QUIT, W_CHEAT, W_WARP, W_READER };

/* DS:0ef2 / 0efc: PROMPTWIN's endings. */
static const char *const go_backs[4] = {
    "to go back.", "if you'd rather not.", "if you changed your mind.", "if you really don't.",
};

static struct {
    int kind;
    surface_t frame;             /* the game's last frame (SAVEPAGE) */
    surface_t screen;            /* the frame with the windows on it */
    int win_x, win_y, win_xmax, win_ymax, top_off, bot_off; /* _WinX... _TopOff, _BotOff */
    int font_space;              /* _FontSpaceLetters */
    int color, prev_color;       /* _FontColorPTR / _PrevFontColorPTR: a table, or -1 plain */
    /* WINDOWCURSOR2 / WINDOWCURSOR */
    int key_phase, key_last, cur_x, cur_y, tick_acc;
    uint8_t ascii_down[128];
    /* WINGETSTR (the warp menu) */
    char text[4];
    int text_len, text_xoff;
    /* the reader */
    uint8_t *file;
    const char *header[100], *body[100];
    int pages, page;
    uint8_t palette[768];        /* the reader's level's palette */
    bool own_palette;
} U;

static uint8_t cursor_frame;     /* DS:53ec */

bool ui_active(void)
{
    return U.kind != W_NONE;
}

const surface_t *ui_screen(void)
{
    return &U.screen;
}

/* DEFAULTPAL: the level's palette has the standard yellow at colour 72 (61, 63, 0); the
 * windows then use colour table 4, else 5, and the cursor is a spinning gem. */
static bool default_pal(void)
{
    const uint8_t *p = U.own_palette ? U.palette : G.level_pal;
    return p[0xD8] == 0x3D && p[0xD9] == 0x3F && p[0xDA] == 0;
}

const uint8_t *ui_palette(void)
{
    return U.kind != W_NONE && U.own_palette ? U.palette : NULL;
}

/* PUTPIC (view coordinates) and PUTWINPIC (window coordinates). */
static void put_pic(const sprite_t *sp, int x, int y)
{
    if (sp)
        draw_sprite(&U.screen, sp, x, y, false);
}

static void win_pic(const sprite_t *sp, int x, int y)
{
    put_pic(sp, U.win_x + x, U.win_y + y);
}

/* FONTSTRLEN: the width of a string in RocFontID, colour codes {..} skipped. */
static int font_len(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        if (*s == '{')
            while (*s && *s != '}')
                s++;
        if (!*s)
            break;
        const int k = (unsigned char)*s < 128 ? font_index[(unsigned char)*s] : -1;
        if (k >= 0 && G.img[IMG_ROCFONT + k])
            w += G.img[IMG_ROCFONT + k]->cols + U.font_space;
    }
    return w;
}

/* SYSPIXELWRITE(x, y, s): at (_WinX + x, _WinY + _TopOff + y); {n} picks colour table n (from
 * 14 up: none, plain pixels), {-} goes back to the one before. */
static void font_write(int x, int y, const char *s)
{
    x += U.win_x;
    y += U.win_y + U.top_off;
    U.color = U.prev_color = 0;
    for (; *s; s++) {
        if (*s == '{') {
            s++;
            if (*s == '-') {
                U.color = U.prev_color;
                while (*s && *s != '}')
                    s++;
            } else {
                int n = 0;
                for (; *s && *s != '}'; s++)
                    if (*s >= '0' && *s <= '9')
                        n = n * 10 + (*s - '0');
                U.prev_color = U.color;
                if (n < 16)
                    U.color = n < 14 ? n : -1;
            }
            if (!*s)
                break;
            continue;
        }
        const int k = (unsigned char)*s < 128 ? font_index[(unsigned char)*s] : -1;
        const sprite_t *sp = k >= 0 ? G.img[IMG_ROCFONT + k] : NULL;
        if (!sp)
            continue;
        if (U.color >= 0) {
            uint8_t map[256];
            for (int v = 0; v < 256; v++) {
                const int i = U.color * 5 + v;
                map[v] = i < (int)sizeof font_colors ? font_colors[i] : (uint8_t)v;
            }
            draw_sprite_mapped(&U.screen, sp, x, y, map);
        } else {
            draw_sprite(&U.screen, sp, x, y, false);
        }
        x += sp->cols + U.font_space;
    }
}

/* MENUSTR(width, y, s): centred in a window `width` columns wide. */
static void menu_str(int width, int y, const char *s)
{
    font_write(width * 4 - font_len(s) / 2, y, s);
}

/* WINCLEARTEXTAREA: the inside of the frame, tiled with the border's fill (frame 4). */
static void clear_text_area(int border)
{
    for (int y = U.win_y + U.top_off + 8; y <= U.win_ymax - 8 + U.bot_off; y += 8)
        for (int x = U.win_x + 8; x <= U.win_xmax - 8; x += 8)
            put_pic(G.img[border + 4], x, y);
}

/* WINUP(col, row, cols, rows, border, top title, bottom title): a frame of 8x8 tiles, centred
 * when col / row is -1, with a title band at the top and / or the bottom. */
static void win_up(int col, int row, int cols, int rows, int border, const char *top,
                   const char *bottom)
{
    const sprite_t *const *b = &G.img[border];
    U.top_off = U.bot_off = 0;
    U.win_x = col < 0 ? 0xA0 - (cols & 0x1FFF) * 4 : col * 8;
    U.win_y = row < 0 ? 100 - (rows & 0x1FFF) * 4 : row * 8;
    U.win_xmax = U.win_x + (cols - 1) * 8;
    U.win_ymax = U.win_y + (rows - 1) * 8;
    put_pic(b[0], U.win_x, U.win_y);
    put_pic(b[9], U.win_x, U.win_ymax);
    put_pic(b[2], U.win_xmax, U.win_y);
    put_pic(b[11], U.win_xmax, U.win_ymax);
    for (int x = U.win_x + 8; x < U.win_xmax; x += 8) {
        put_pic(b[1], x, U.win_y);
        put_pic(b[10], x, U.win_ymax);
    }
    for (int y = U.win_y + 8; y < U.win_ymax; y += 8) {
        put_pic(b[3], U.win_x, y);
        put_pic(b[5], U.win_xmax, y);
    }
    if (top) {
        const int w = font_len(top);
        U.top_off = 0x10;
        put_pic(b[6], U.win_x, U.win_y + 0x10);
        put_pic(b[8], U.win_xmax, U.win_y + 0x10);
        for (int x = U.win_x + 8; x < U.win_xmax; x += 8) {
            put_pic(b[7], x, U.win_y + 0x10);
            put_pic(b[4], x, U.win_y + 8);
        }
        font_write((cols & 0x1FFF) * 4 - w / 2, -8, top);
    }
    if (bottom) {
        const int w = font_len(bottom);
        U.bot_off = -0x10;
        put_pic(b[6], U.win_x, U.win_ymax - 0x10);
        put_pic(b[8], U.win_xmax, U.win_ymax - 0x10);
        for (int x = U.win_x + 8; x < U.win_xmax; x += 8) {
            put_pic(b[7], x, U.win_ymax - 0x10);
            put_pic(b[4], x, U.win_ymax - 8);
        }
        font_write((cols & 0x1FFF) * 4 - w / 2, (rows - 4) * 8, bottom);
    }
    clear_text_area(border);
}

/* ANIMATECURSOR: the next frame of the cursor (CursorTID, or the spinning gem CollectID[18..21]
 * on the standard palette). */
static void animate_cursor(int x, int y)
{
    const int set = default_pal() ? IMG_COLLECT + 18 : IMG_CURSORT;
    if (++cursor_frame > 3)
        cursor_frame = 0;
    win_pic(G.img[set + cursor_frame], x, y);
}

static int any_key(void)
{
    for (int k = 1; k < 0x54; k++)
        if (G.keys[k])
            return k;
    return 0;
}

/* WINDOWCURSOR2(x, y): once every key is let go, the scancode of the next key pressed and let
 * go; the cursor at (x + 8, y + 0x18) turns every 20 ticks meanwhile. 0 while waiting; 1 when
 * the pause key ends a pause. */
static int cursor2(int ticks)
{
    if (U.key_phase == 0) {
        if (!any_key())
            U.key_phase = 1;
        return 0;
    }
    if (U.key_last == 0)
        U.key_last = any_key();
    else if (!G.keys[U.key_last])
        return U.key_last;
    if (U.kind == W_STATUS && G.pause_hit)
        return 1;
    for (U.tick_acc += ticks; U.tick_acc >= 20; U.tick_acc -= 20)
        animate_cursor(U.cur_x + 8, U.cur_y + 0x18);
    return 0;
}

/* WINDOWCURSOR(x, y) for typing: the character of each key pressed (the port's stand-in for
 * the BIOS keyboard: letters, digits, Enter, Esc, Backspace), the cursor turning meanwhile. */
static int cursor_type(int ticks)
{
    static const char row1[] = "1234567890", row2[] = "qwertyuiop", row3[] = "asdfghjkl",
                      row4[] = "zxcvbnm";
    int ch = 0;
    for (int k = 1; k < 0x54 && !ch; k++) {
        const bool down = G.keys[k] != 0;
        if (down && !U.ascii_down[k]) {
            if (k >= 0x02 && k <= 0x0B)
                ch = row1[k - 0x02];
            else if (k >= 0x10 && k <= 0x19)
                ch = row2[k - 0x10] - 'a' + 'A';
            else if (k >= 0x1E && k <= 0x26)
                ch = row3[k - 0x1E] - 'a' + 'A';
            else if (k >= 0x2C && k <= 0x32)
                ch = row4[k - 0x2C] - 'a' + 'A';
            else if (k == 0x1C)
                ch = '\r';
            else if (k == 0x01)
                ch = 0x1B;
            else if (k == 0x0E)
                ch = '\b';
        }
        U.ascii_down[k] = down;
    }
    if (!ch)
        for (U.tick_acc += ticks; U.tick_acc >= 20; U.tick_acc -= 20)
            animate_cursor(U.text_xoff + 1 + 8, 2 + 1 + 0x18);
    return ch;
}

static void begin(int kind, int cur_x, int cur_y)
{
    U.kind = kind;
    U.key_phase = 0;
    U.key_last = 0;
    U.tick_acc = 0;
    U.cur_x = cur_x;
    U.cur_y = cur_y;
}

static void fresh_screen(void)
{
    memcpy(U.screen.px, U.frame.px, (size_t)U.frame.w * U.frame.h);
}

/* RXWINDF.C:961 STATUSWIN: the pause window: both heroes' hit points (and poison), the level
 * (episode, number and name, from rocdesc.txt), the gems and the score. */
static void open_status(void)
{
    win_up(-1, -1, 0x1D, 10, IMG_BORDER2, NULL, NULL);
    char desc[31] = "";
    size_t len;
    uint8_t *d = G.arc ? arc_load(G.arc, "rocdesc.txt", &len) : NULL;
    if (d && (size_t)G.level * 30 + 30 <= len)
        snprintf(desc, sizeof desc, "%.30s", (const char *)d + G.level * 30);
    free(d);
    G.pause_hit = 0;
    G.main_pausing = 1;
    win_pic(G.img[IMG_MENUHEAD], 2, 0);
    win_pic(G.img[IMG_MENUHEAD + 1], 0x7A, 0);
    font_write(0x36, 0, "{6}Endrick");
    font_write(0xAF, 0, "{6}Elandra");
    for (int x = 0; x < 2; x++) {
        const int max = Inv.max_hit_points[x];
        const int base = max == 4 ? 0 : max == 6 ? 5 : max == 8 ? 12 : max == 10 ? 21 : -1;
        if (base >= 0)
            win_pic(G.img[IMG_HP + base + Inv.hit_points[x]], x == 0 ? 0xAF : 0x36, 10);
    }
    if (Inv.flags & 1)
        font_write(0x36, 0x21, "{6}Poisoned!");
    if (Inv.flags & 2)
        font_write(0xAF, 0x21, "{6}Poisoned!");
    int sub = G.level + 1;
    const char *episode = "1";
    if (G.level >= 0x11) {
        episode = "3";
        sub = G.level - 0x10;
    } else if (G.level >= 8) {
        episode = "2";
        sub = G.level - 7;
    }
    char num[8], s[96];
    if (G.level < 0x1A)
        snprintf(num, sizeof num, "%d", sub);
    else
        snprintf(num, sizeof num, "S");
    snprintf(s, sizeof s, "{8}E%sL%s: {7}%s", episode, num, desc);
    font_write(0x74 - font_len(s) / 2, 0x37, s);
    snprintf(s, sizeof s, "{8}Gems: {7}%u   {8}Score: {7}%lu", Inv.gems, (unsigned long)Inv.score);
    font_write(0x74 - font_len(s) / 2, 0x41, s);
    begin(W_STATUS, 0xD0, 0x30);
}

/* RXWINDF.C:900 PROMPTWIN(0): "quit to DOS?" Y quits, any other key goes back. */
static void open_quit(void)
{
    const char *c = default_pal() ? "{4}" : "{5}";
    char s[96];
    snprintf(s, sizeof s, "%sARE YOU SURE?", c);
    win_up(-1, -1, 0x18, 0x0B, IMG_BORDER2, s, NULL);
    snprintf(s, sizeof s, "%sPress \"Y\" if you really", c);
    menu_str(0x18, 0x10, s);
    snprintf(s, sizeof s, "%swant to quit to DOS.", c);
    menu_str(0x18, 0x19, s);
    snprintf(s, sizeof s, "%sPress any other key", c);
    menu_str(0x18, 0x29, s);
    snprintf(s, sizeof s, "%s%s", c, go_backs[frandom(4)]);
    menu_str(0x18, 0x32, s);
    begin(W_QUIT, 0xAC, 0x34);
}

/* RXWINDF.C:1963 CHEATER(n): 0 everything, 1 ten hit points for both, 2 at least 250 gems, 3
 * the strongest weapons, 4 healed, 5 invincibility on or off. */
static void open_cheat(int cheat)
{
    win_up(-1, -1, 0x16, 0x0C, IMG_BORDER2, "{4}CHEAT CODE", NULL);
    int y = 0x10;
    const char *line;
    if (cheat == 0 || cheat == 1) {
        line = "{4}Stamina fully powered up!";
        font_write(0x58 - font_len(line) / 2, 0x10, line);
        y = 0x19;
        Inv.hit_points[0] = Inv.max_hit_points[0] = 10;
        Inv.hit_points[1] = Inv.max_hit_points[1] = 10;
    }
    if (cheat == 0 || cheat == 2) {
        line = "{4}Mana Gems rounded up to 250!";
        font_write(0x58 - font_len(line) / 2, y, line);
        y += 9;
        if (Inv.gems < 0xFA)
            Inv.gems = 0xFA;
    }
    if (cheat == 0 || cheat == 3) {
        line = "{4}Weapons fully powered up!";
        font_write(0x58 - font_len(line) / 2, y, line);
        y += 9;
        Inv.flags = (uint8_t)((Inv.flags & 0xC3) | 0x3C);
    }
    if (cheat == 0 || cheat == 4) {
        line = "{4}Health points have been recharged!";
        font_write(0x58 - font_len(line) / 2, y, line);
        y += 9;
        Inv.hit_points[0] = Inv.max_hit_points[0];
        Inv.hit_points[1] = Inv.max_hit_points[1];
    }
    if (cheat == 5) {
        line = G.you_invincible ? "{4}Invincibility toggled OFF!" : "{4}Invincibility turned ON!";
        G.you_invincible = !G.you_invincible;
        font_write(0x58 - font_len(line) / 2, y, line);
        y += 9;
    }
    begin(W_CHEAT, 0xAE, y - 8);
}

/* RXWINDF.C:2017 WARPMENU: type a level number (WINGETSTR, two characters in colour 7). */
static void open_warp(void)
{
    win_up(-1, -1, 0x14, 6, IMG_BORDER, "{4}Enter level (1-26):", NULL);
    U.text[0] = 0;
    U.text_len = 0;
    U.text_xoff = 2;
    memset(U.ascii_down, 1, sizeof U.ascii_down); /* (the key that opened it is still down) */
    begin(W_WARP, 0, 0);
}

/* RXWINDF.C:1855 WINGETSTR's typing, a character at a time. Returns 1 when Enter ends it, -1 for
 * Esc. */
static int warp_char(int ch)
{
    const int border = default_pal() ? IMG_BORDER : IMG_BORDERT;
    if (ch == '\b') {
        if (U.text_len > 0) {
            win_pic(G.img[border + 4], U.text_xoff + 9, 2 + 0x19);
            char s[8];
            snprintf(s, sizeof s, "%s%c", default_pal() ? "{10}" : "{11}",
                     U.text[U.text_len - 1]);
            U.text_xoff -= font_len(s);
            font_write(U.text_xoff + 8, 2 + 8, s);
            U.text[--U.text_len] = 0;
        }
        return 0;
    }
    if (ch == 0x1B)
        return -1;
    if (ch == '\r') {
        win_pic(G.img[border + 4], U.text_xoff + 9, 2 + 0x19);
        return 1;
    }
    if (U.text_len > 1)
        return 0;
    win_pic(G.img[border + 4], U.text_xoff + 9, 2 + 0x19);
    const char one[2] = {(char)ch, 0};
    U.text_xoff += font_len(one);
    U.text[U.text_len++] = (char)ch;
    U.text[U.text_len] = 0;
    char s[16];
    snprintf(s, sizeof s, "{7}%s", U.text);
    font_write(2 + 8, 2 + 8, s);
    return 0;
}

/* The page reader (PAGEREADER, READERWIN): a page is a title and a text, each NUL-ended. */
static bool reader_load(const char *name)
{
    size_t size;
    free(U.file);
    U.file = G.arc ? arc_load(G.arc, name, &size) : NULL;
    if (!U.file)
        return false;
    U.pages = 0;
    size_t p = 0;
    while (p < size && U.pages < 100) {
        const size_t hl = strnlen((const char *)U.file + p, size - p);
        if (p + hl >= size)
            break;
        const size_t bl = strnlen((const char *)U.file + p + hl + 1, size - p - hl - 1);
        if (p + hl + 1 + bl >= size)
            break;
        U.header[U.pages] = (const char *)U.file + p;
        U.body[U.pages] = (const char *)U.file + p + hl + 1;
        U.pages++;
        p += hl + 1 + bl + 1;
    }
    return U.pages > 0;
}

/* RXWINDF.C:1704 READERWIN: the page in a 25 x 39 window, the title on top and "Page n of m"
 * with the keys underneath; {Txxx} moves to column xxx, {Pxxxyyynn} puts a picture (InstIllID[nn],
 * or CollectID[nn - 10]) at (xxx, yyy); the text is centred on its widest line. */
static void reader_page(void)
{
    char foot[128];
    snprintf(foot, sizeof foot, "{6}Page %d of %d {9}", U.page + 1, U.pages);
    strcat(foot, "PgUp "); /* (rocinst.txt wraps round, so both always show) */
    strcat(foot, "PgDn ");
    strcat(foot, "{8}(Press ESC to exit)");
    fresh_screen();
    U.font_space = -1;
    win_up(-1, -1, 0x27, 0x19, IMG_BORDER2, U.header[U.page], foot);
    U.font_space = 0;
    struct { int x, y; char s[128]; } line[25];
    int n = 0, len = 0, ypos = 0;
    line[0].x = line[0].y = 0;
    for (const char *ch = U.body[U.page];; ch++) {
        if (ch[0] == '{' && ch[1] == 'T' && strlen(ch) > 5 && ch[5] == '}') {
            line[n].s[len] = 0;
            if (n < 24)
                n++;
            len = 0;
            line[n].x = (ch[2] - '0') * 100 + (ch[3] - '0') * 10 + (ch[4] - '0');
            line[n].y = ypos;
            ch += 6;
        }
        if (ch[0] == '{' && ch[1] == 'P' && strlen(ch) > 10 && ch[10] == '}') {
            const int k = (ch[8] - '0') * 10 + (ch[9] - '0');
            const sprite_t *sp = k < 10 ? G.img[IMG_INSTILL + k] : G.img[IMG_COLLECT + k - 10];
            win_pic(sp, (ch[2] - '0') * 100 + (ch[3] - '0') * 10 + (ch[4] - '0'),
                    (ch[5] - '0') * 100 + (ch[6] - '0') * 10 + (ch[7] - '0'));
            ch += 11;
        }
        if (len < 127)
            line[n].s[len++] = *ch;
        if (*ch == '\n') {
            line[n].s[len] = 0;
            if (n < 24)
                n++;
            len = 0;
            line[n].x = 0;
            ypos += 9;
            line[n].y = ypos;
        } else if (*ch == 0) {
            int widest = 0;
            for (int i = 0; i <= n; i++)
                if (font_len(line[i].s) > widest)
                    widest = font_len(line[i].s);
            for (int i = 0; i <= n; i++)
                font_write(0x9C - widest / 2 + line[i].x, line[i].y + 0x0C, line[i].s);
            break;
        }
    }
    begin(W_READER, 0x124, 0xA1);
}

void ui_open(int menu, const surface_t *frame, const uint8_t *palette)
{
    if (!U.frame.px && (!surface_init(&U.frame, frame->w, frame->h) ||
                        !surface_init(&U.screen, frame->w, frame->h)))
        return;
    static const int sets[] = {IMG_ROCFONT, IMG_BORDER, IMG_BORDER2, IMG_BORDERT, IMG_CURSORT,
                               IMG_COLLECT, IMG_MENUHEAD, IMG_HP, IMG_INSTILL};
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++)
        game_need_set(sets[i]);
    memcpy(U.frame.px, frame->px, (size_t)frame->w * frame->h);
    U.own_palette = palette != NULL;
    if (palette)
        memcpy(U.palette, palette, sizeof U.palette);
    fresh_screen();
    U.font_space = 0;
    if (menu == MENU_STATUS)
        open_status();
    else if (menu == MENU_QUIT)
        open_quit();
    else if (menu >= MENU_CHEAT && menu <= MENU_CHEAT + 5)
        open_cheat(menu - MENU_CHEAT);
    else if (menu == MENU_WARP)
        open_warp();
    else if (menu == MENU_HELP && reader_load("rocinst.txt")) {
        U.page = 11 < U.pages ? 11 : U.pages - 1;
        reader_page();
    }
}

ui_result_t ui_update(int ticks, int *level)
{
    switch (U.kind) {
    case W_STATUS:
    case W_CHEAT:
        if (cursor2(ticks)) {
            U.kind = W_NONE;
            G.pause_hit = 0;
            G.main_pausing = 0;
        }
        return UI_CONTINUE;
    case W_QUIT: {
        const int k = cursor2(ticks);
        if (!k)
            return UI_CONTINUE;
        U.kind = W_NONE;
        return k == 0x15 ? UI_QUIT : UI_CONTINUE; /* Y */
    }
    case W_WARP: {
        const int ch = cursor_type(ticks);
        const int r = ch ? warp_char(ch) : 0;
        if (r == 0)
            return UI_CONTINUE;
        U.kind = W_NONE;
        if (r < 0 || U.text_len == 0)
            return UI_CONTINUE;
        /* "the level's number": one or two characters, as the original computes it */
        uint8_t v = (uint8_t)U.text[0];
        if (U.text_len != 1)
            v = (uint8_t)(U.text[1] + (U.text[0] - '0') * 10);
        if ((uint8_t)(v - '0') >= 0x1C || (uint8_t)(v - '0') == 0)
            return UI_CONTINUE; /* (0 would be level -1 in the original) */
        *level = (uint8_t)(v - '0') - 1;
        return UI_WARP;
    }
    case W_READER: {
        int k = cursor2(ticks);
        if (!k)
            return UI_CONTINUE;
        if ((k == 0x1C && U.page == U.pages - 1) || k == (G.key_trans[K_JUMP] & 0x7F))
            k = 1;
        else if (k == (G.key_trans[K_ATTACK] & 0x7F))
            k = 0x1C;
        const int old = U.page;
        switch (k) {
        case 0x01: U.page = -1; break;
        case 0x47: U.page = 0; break;
        case 0x4F: U.page = U.pages - 1; break;
        case 0x0C: case 0x48: case 0x49: case 0x4A: case 0x4B:
            U.page = U.page > 0 ? U.page - 1 : U.pages - 1;
            break;
        case 0x0D: case 0x1C: case 0x39: case 0x4D: case 0x4E: case 0x50: case 0x51:
            U.page = U.page < U.pages - 1 ? U.page + 1 : 0;
            break;
        default: break;
        }
        if (U.page < 0) {
            U.kind = W_NONE;
            free(U.file);
            U.file = NULL;
        } else if (U.page != old) {
            reader_page();
        } else {
            U.key_phase = 0;
            U.key_last = 0;
        }
        return UI_CONTINUE;
    }
    default:
        return UI_CONTINUE;
    }
}
