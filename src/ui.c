/* The windows and menus (RXWINDF.C, and WINUP / SYSPIXELWRITE / FONTSTRLEN / WINCLEARTEXTAREA /
 * PUTWINPIC from the window assembly at 3f50). See ui.h. */
#include "ui.h"

#include "almidi.h"
#include "audio.h"
#include "game.h"
#include "host.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DS:1fb2: the font frame (RocFontID, BigFontID) for each character, -1 for none. */
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
 * (the blitter's xlat can read past a table into the next). The menus flash the first entry of
 * table 0 or 1 (the selected line). */
static uint8_t font_colors[14 * 5] = {
    23, 72, 74, 76, 78, 23, 8,  10, 12, 14,  23, 73, 75, 77, 79,  23, 9,  11, 13, 15,
    23, 72, 74, 76, 78, 23, 8,  10, 12, 14,  23, 97, 98, 99, 100, 23, 30, 29, 28, 27,
    23, 48, 49, 50, 51, 23, 105, 106, 107, 108, 53, 53, 53, 53, 53, 1,  1,  1,  1,  1,
    23, 16, 17, 18, 19, 23, 72, 72, 72, 72,
};

/* DS:0ef2 / 0efc: PROMPTWIN's questions and endings. */
static const char *const prompts[5] = {
    "quit to DOS.", "end this game.", "restore", "QuickSave over", "QuickRestore",
};
static const char *const go_backs[4] = {
    "to go back.", "if you'd rather not.", "if you changed your mind.", "if you really don't.",
};

static struct {
    surface_t page;              /* SAVEPAGE: the screen under the window */
    int win_x, win_y, win_xmax, win_ymax, top_off, bot_off; /* _WinX... _TopOff, _BotOff */
    int font;                    /* _SysFontP: IMG_ROCFONT or IMG_BIGFONT */
    int font_space;              /* _FontSpaceLetters */
    int color, prev_color;       /* _FontColorPTR / _PrevFontColorPTR: a table, or -1 plain */
    uint8_t menu_pos[8];         /* DS:53e0: each menu's last line */
    uint8_t cursor_frame;        /* DS:53ec */
    uint8_t blood_temp[20];      /* DS:53ce: a new blood password, typed once */
    int menu_voc, menu_voc2;     /* _MenuVOChandle / 2: the menus' sounds playing */
    uint8_t volume_line;         /* DS:53eb: the sound menu's line */
} U = {.font = IMG_ROCFONT};

/* A menu line moved (_MenuVOC, sample 3) or picked (_MenuVOC2, 0xa9). */
static void menu_click(void)
{
    if (fx_active(U.menu_voc))
        fx_stop(U.menu_voc);
    U.menu_voc = sound_fx(3);
}

static void menu_pick(void)
{
    if (fx_active(U.menu_voc2))
        fx_stop(U.menu_voc2);
    U.menu_voc2 = sound_fx(0xA9);
}

/* The sets the windows draw with (the levels' object lists load most of them). */
static void need_sets(void)
{
    static const int sets[] = {IMG_ROCFONT, IMG_BIGFONT, IMG_BORDER, IMG_BORDER2, IMG_BORDERT,
                               IMG_CURSORT, IMG_COLLECT, IMG_MENUHEAD, IMG_HP, IMG_INSTILL,
                               IMG_SLIDER};
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++)
        game_need_set(sets[i]);
}

bool default_pal(void)
{
    const uint8_t *p = G.level_pal;
    return p[0xD8] == 0x3D && p[0xD9] == 0x3F && p[0xDA] == 0;
}

void gamma_key(uint8_t *armed)
{
    if (!G.keys[0x57] || !*armed) {
        if (!G.keys[0x57])
            *armed = 1;
        return;
    }
    *armed = 0;
    G.gamma = (uint8_t)(G.gamma + 4);
    if (G.gamma > 0x10)
        G.gamma = 0;
}

/* PUTPIC (screen coordinates) and PUTWINPIC (window coordinates). */
static void put_pic(const sprite_t *sp, int x, int y)
{
    if (sp)
        draw_sprite(&Vga, sp, x, y, false);
}

static void win_pic(const sprite_t *sp, int x, int y)
{
    put_pic(sp, U.win_x + x, U.win_y + y);
}

/* FONTSTRLEN: the width of a string in the font, colour codes {..} skipped. */
static int font_len_in(int font, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        if (*s == '{')
            while (*s && *s != '}')
                s++;
        if (!*s)
            break;
        const int k = (unsigned char)*s < 128 ? font_index[(unsigned char)*s] : -1;
        if (k >= 0 && G.img[font + k])
            w += G.img[font + k]->cols + U.font_space;
    }
    return w;
}

static int font_len(const char *s)
{
    return font_len_in(U.font, s);
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
        const sprite_t *sp = k >= 0 ? G.img[U.font + k] : NULL;
        if (!sp)
            continue;
        if (U.color >= 0) {
            uint8_t map[256];
            for (int v = 0; v < 256; v++) {
                const int i = U.color * 5 + v;
                map[v] = i < (int)sizeof font_colors ? font_colors[i] : (uint8_t)v;
            }
            draw_sprite_mapped(&Vga, sp, x, y, map);
        } else {
            draw_sprite(&Vga, sp, x, y, false);
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

/* WINUP(col, row, cols, rows, border, top title, bottom title): PAUSEACTION, SAVEPAGE, then a
 * frame of 8x8 tiles, centred when col / row is -1, with a title band at the top and / or the
 * bottom. */
static void win_up(int col, int row, int cols, int rows, int border, const char *top,
                   const char *bottom)
{
    need_sets();
    if (!U.page.px && !surface_init(&U.page, SCREEN_W, SCREEN_H))
        return;
    memcpy(U.page.px, Vga.px, (size_t)SCREEN_W * SCREEN_H);
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
        U.top_off = 0;
        font_write((cols & 0x1FFF) * 4 - w / 2, 8, top);
        U.top_off = 0x10;
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

/* WINDOWN leaves the window on the screen (and the game runs on); WINCLEAR also puts back the
 * screen it covered (REFRESHPAGE). */
static void win_down(void)
{
}

void win_clear(void)
{
    win_down();
    if (U.page.px)
        memcpy(Vga.px, U.page.px, (size_t)SCREEN_W * SCREEN_H);
}

/* ANIMATECURSOR: the next frame of the cursor (CursorTID, or the spinning gem CollectID[18..21]
 * on the standard palette). */
static void animate_cursor(int x, int y)
{
    maintain_sound();
    const int set = default_pal() ? IMG_COLLECT + 18 : IMG_CURSORT;
    if (++U.cursor_frame > 3)
        U.cursor_frame = 0;
    win_pic(G.img[set + U.cursor_frame], x, y);
}

static int any_key(void)
{
    for (int k = 1; k < 0x54; k++)
        if (G.keys[k])
            return k;
    return 0;
}

/* Wait for every key to be let go (the windows' "while (any key) ;"). */
static void wait_keys_up(void)
{
    while (any_key())
        host_frame();
}

/* WINDOWCURSOR2(x, y): once every key is let go, the scancode of the next key pressed and let
 * go; the cursor at (x + 8, y + 0x18) turns every 20 ticks meanwhile, and F11 works. A pause
 * (STATUSWIN's) also ends on the Pause key: 1. */
static int window_cursor2(int x, int y, bool pause)
{
    uint8_t f11 = 0;
    wait_keys_up();
    int last = 0, acc = 0;
    for (;;) {
        const int ticks = host_frame();
        gamma_key(&f11);
        if (pause && G.pause_hit)
            return 1;
        if (last == 0)
            last = any_key();
        else if (!G.keys[last])
            return last;
        for (acc += ticks; acc >= 20; acc -= 20)
            animate_cursor(x + 8, y + 0x18);
    }
}

/* WINDOWCURSOR(x, y): the next character typed (GETKEY), the cursor turning every 20 ticks. */
static int window_cursor(int x, int y)
{
    int acc = 0;
    for (;;) {
        const int c = host_getkey();
        if (c)
            return c;
        for (acc += host_frame(); acc >= 20; acc -= 20)
            animate_cursor(x + 8, y + 0x18);
    }
}

/* RXWINDF.C:1855 WINGETSTR(str, x, y, max, colour, secret): type into `str` (which may start
 * with text) at (x + 8, y + 8) in font colour `colour`, `*` for each character if secret.
 * Enter ends it; Esc leaves "\x01". */
static void win_get_str(char *str, int x, int y, int max, int color, bool secret)
{
    host_flush_keys();
    const int border = default_pal() ? IMG_BORDER : IMG_BORDERT;
    char color_str[8], s[96];
    snprintf(color_str, sizeof color_str, "{%d}", color);
    int len = (int)strlen(str);
    int xoff = x + font_len(str);
    for (;;) {
        const int ch = window_cursor(xoff + 1, y + 1);
        if (ch == '\b') {
            if (x < xoff) {
                win_pic(G.img[border + 4], xoff + 9, y + 0x19);
                snprintf(s, sizeof s, "%s%c", default_pal() ? "{10}" : "{11}",
                         secret ? '*' : str[len - 1]);
                xoff -= font_len(s);
                font_write(xoff + 8, y + 8, s);
                str[--len] = 0;
            }
            continue;
        }
        if (ch == 0x1B) {
            str[0] = 1;
            break;
        }
        if (ch == '\r') {
            str[len] = 0;
            win_pic(G.img[border + 4], xoff + 9, y + 0x19);
            break;
        }
        if (ch < 0x20 || ch > 0x7A || (ch > 0x5A && ch < 0x61) || (ch > 0x3A && ch < 0x3F) ||
            ch == '@' || ch == '+' || ch == '%' || ch == '#' || len > max - 1)
            continue;
        win_pic(G.img[border + 4], xoff + 9, y + 0x19);
        const char one[2] = {secret ? '*' : (char)ch, 0};
        xoff += font_len(one);
        str[len++] = (char)ch;
        str[len] = 0;
        snprintf(s, sizeof s, "%s", color_str);
        for (int i = 0; i < len; i++) {
            const size_t n = strlen(s);
            if (n + 1 < sizeof s) {
                s[n] = secret ? '*' : str[i];
                s[n + 1] = 0;
            }
        }
        font_write(x + 8, y + 8, s);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Menus                                                                                       */

/* The scancode of each letter, for the menus' first-letter keys (SELECTMENU's LineKey). */
static int letter_key(char c)
{
    static const uint8_t keys[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                     0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                     0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 0x20);
    if (c >= 'A' && c <= 'Z')
        return keys[c - 'A'];
    return c == '-' ? 0x0C : 0x39;
}

static void menu_line(const char *const *menu, int y, bool selected)
{
    char s[192];
    const bool d = default_pal();
    snprintf(s, sizeof s, "%s%s", selected ? (d ? "{0}" : "{1}") : (d ? "{2}" : "{3}"), menu[y]);
    menu_str(0x28, y * 0x10 + 0x1E, s);
}

/* RXWINDF.C:442 OPENMENU(menu, width, height, title): the big font, a window with the title,
 * the lines 16 pixels apart; then it waits for every key to be let go. */
static void open_menu(const char *const *menu, int width, int height, const char *title)
{
    U.font = IMG_BIGFONT;
    U.font_space = -1;
    win_up(-1, -1, width, height, IMG_BORDER2, title, NULL);
    for (int line = 0; menu[line]; line++)
        menu_line(menu, line, false);
    wait_keys_up();
}

/* RXWINDF.C:871 CLOSEMENU */
static void close_menu(void)
{
    U.font = IMG_ROCFONT;
    U.font_space = 0;
    wait_tics(2);
    /* the menu's sounds play out */
    for (int t = 0; t < 5000 && (fx_active(U.menu_voc) || fx_active(U.menu_voc2));)
        t += host_frame();
    win_clear();
}

/* RXWINDF.C:492 SELECTMENU(menu, number): up / down (or a line's first letter) move, Enter or
 * the attack key picks (1-based), Esc or the jump key leaves (100). The selected line's outline
 * glows (a step every 20 ticks), and a held key repeats every 7 steps. Each menu starts on its
 * last line (the difficulty menu on Normal). */
static int select_menu(const char *const *menu, int num)
{
    int lines = 0;
    int line_key[9];
    for (; lines < 9 && menu[lines]; lines++)
        line_key[lines] = letter_key(menu[lines][0]);
    int y = num == 2 ? 2 : U.menu_pos[num];
    if (lines - 1 < y)
        y = 0;
    bool up_ok = false, down_ok = false, letter_ok = false, moved = false, enter_ok = false,
         esc_ok = false, attack_ok = false, jump_ok = false, f11_ok = false;
    int select = 0, flash = 0, timecounter = 0;
    for (;;) {
        if (select) {
            U.menu_pos[num] = (uint8_t)y;
            G.pause_hit = 0;
            return select;
        }
        if (++flash == 0x10)
            flash = 0;
        if (default_pal())
            font_colors[0] = (uint8_t)(flash < 8 ? flash + 0x40 : 0x4F - flash);
        else
            font_colors[5] = (uint8_t)(flash < 8 ? flash + 0x18 : 0x27 - flash);
        menu_line(menu, y, true);
        if (++timecounter > 6) {
            timecounter = 0;
            up_ok = down_ok = true;
        }
        for (int count = 0; count < 0x14;) {
            if (up_ok && key(K_UP)) {
                up_ok = false;
                menu_line(menu, y, false);
                y = y > 0 ? y - 1 : lines - 1;
                menu_line(menu, y, true);
                moved = true;
            } else if (down_ok && key(K_DOWN)) {
                down_ok = false;
                menu_line(menu, y, false);
                if (++y == lines)
                    y = 0;
                menu_line(menu, y, true);
                moved = true;
            } else if (letter_ok) {
                for (int x = 0; x < lines; x++) {
                    if (!moved && G.keys[line_key[x]] && x != y) {
                        moved = true;
                        letter_ok = false;
                        menu_line(menu, y, false);
                        do {
                            if (++y == lines)
                                y = 0;
                        } while (line_key[y] != line_key[x]);
                        menu_line(menu, y, true);
                    }
                }
            }
            if (moved) {
                menu_click();
                timecounter = 0;
                count = 0;
                moved = false;
            }
            if ((!G.keys[0x1C] && enter_ok) || (!key(K_ATTACK) && attack_ok)) {
                menu_pick();
                enter_ok = attack_ok = false;
                select = y + 1;
                count = 0x14;
            } else if ((!G.keys[0x01] && esc_ok) || (!key(K_JUMP) && jump_ok)) {
                esc_ok = jump_ok = false;
                select = 100;
                count = 0x14;
            }
            if (G.keys[0x57] && f11_ok) {
                f11_ok = false;
                G.gamma = (uint8_t)(G.gamma + 4);
                if (G.gamma > 0x10)
                    G.gamma = 0;
            }
            if (key(K_ATTACK))
                attack_ok = true;
            if (key(K_JUMP))
                jump_ok = true;
            if (G.keys[0x1C])
                enter_ok = true;
            if (G.keys[0x01])
                esc_ok = true;
            if (!G.keys[0x57])
                f11_ok = true;
            if (!key(K_UP))
                up_ok = true;
            if (!key(K_DOWN))
                down_ok = true;
            if (!letter_ok) {
                letter_ok = true;
                for (int x = 0; x < lines; x++)
                    if (G.keys[line_key[x]])
                        letter_ok = false;
            }
            if (count < 0x14)
                count += host_frame();
        }
    }
}

/* The sound menu's sliders: the track (SliderID 0, or 2 off the standard palette) and the knob
 * (1 or 3) at volume / 4; VOLUMESELECT draws a knob moved up one pixel further right. */
static void slider(int line, int volume, int nudge)
{
    const bool d = default_pal();
    win_pic(G.img[IMG_SLIDER + (d ? 0 : 2)], 0x79, line ? 0x5D : 0x3D);
    win_pic(G.img[IMG_SLIDER + (d ? 1 : 3)], volume / 4 + 0x7D + nudge, line ? 0x60 : 0x40);
}

/* RXWINDF.C:707 VOLUMESELECT: up / down (or S, M) pick the line, left / right move its slider a
 * step of 16 (the volume changes at once, with the menu's click), Esc or the jump key leaves. */
static void volume_select(const char *const *menu)
{
    int y = U.volume_line;
    bool right_ok = false, left_ok = false, up_ok = false, down_ok = false, letter_ok = false,
         moved = false, esc_ok = false, jump_ok = false, f11_ok = false;
    int flash = 0, timecounter = 0, timecounter2 = 0;
    for (;;) {
        if (++flash == 0x10)
            flash = 0;
        if (default_pal())
            font_colors[0] = (uint8_t)(flash < 8 ? flash + 0x40 : 0x4F - flash);
        else
            font_colors[5] = (uint8_t)(flash < 8 ? flash + 0x18 : 0x27 - flash);
        menu_line(menu, y, true);
        if (++timecounter > 6) {
            timecounter = 0;
            up_ok = down_ok = true;
        }
        if (++timecounter2 > 2) {
            timecounter2 = 0;
            right_ok = left_ok = true;
        }
        for (int count = 0; count < 0x14;) {
            int16_t *vol = y == 0 ? &Sess.sound_volume : &Sess.music_volume;
            int step = 0;
            if (key(K_RIGHT) && right_ok) {
                right_ok = false;
                step = 0x10;
            } else if (key(K_LEFT) && left_ok) {
                left_ok = false;
                step = -0x10;
            }
            if (step) {
                timecounter2 = 0;
                *vol = (int16_t)(*vol + step);
                *vol = (int16_t)(*vol < 0 ? 0 : *vol > 0xFF ? 0xFF : *vol);
                slider(y, *vol, step > 0);
                fx_set_volume(Sess.sound_volume);
                music_set_volume(Sess.music_volume);
                timecounter2 = 0;
                menu_click();
            }
            if (key(K_UP) && up_ok) {
                moved = true;
                up_ok = false;
            } else if (key(K_DOWN) && down_ok) {
                moved = true;
                down_ok = false;
            } else if ((G.keys[0x1F] && y != 0) || (G.keys[0x32] && y == 0 && letter_ok)) {
                moved = true;
                letter_ok = false;
                y = G.keys[0x1F] ? 2 : 0;
            }
            if (moved) {
                menu_line(menu, y, false);
                y = y == 0 ? 2 : 0;
                menu_line(menu, y, true);
                menu_click();
                timecounter = 0;
                count = 0;
                moved = false;
            }
            if ((G.keys[0x01] && esc_ok) || (!key(K_JUMP) && jump_ok)) {
                U.volume_line = (uint8_t)y;
                return;
            }
            if (G.keys[0x57] && f11_ok) {
                f11_ok = false;
                G.gamma = (uint8_t)(G.gamma + 4);
                if (G.gamma > 0x10)
                    G.gamma = 0;
            }
            if (key(K_JUMP))
                jump_ok = true;
            if (!G.keys[0x01])
                esc_ok = true;
            if (!key(K_UP))
                up_ok = true;
            if (!key(K_DOWN))
                down_ok = true;
            if (!key(K_RIGHT))
                right_ok = true;
            if (!key(K_LEFT))
                left_ok = true;
            if (!G.keys[0x57])
                f11_ok = true;
            if (!G.keys[0x1F] && !G.keys[0x32])
                letter_ok = true;
            count += host_frame();
        }
    }
}

/* RXWINDF.C:415 SOUNDMENU: the effects' and the music's volume. */
void sound_menu(void)
{
    static const char *const menu[] = {"Sound FX Volume", " ", "Music Volume", " ", NULL};
    open_menu(menu, 0x28, 0x12, default_pal() ? "{2}SOUND CONTROLS" : "{3}SOUND CONTROLS");
    slider(0, Sess.sound_volume, 0);
    slider(2, Sess.music_volume, 0);
    volume_select(menu);
    close_menu();
}

/* RXWINDF.C:150 DIFFICULTYMENU: the episode, then the difficulty (Esc there goes back to the
 * episodes); then GAMEINIT and _quitflag 7 starts the episode. */
static void difficulty_menu(void)
{
    static const char *const episodes[] = {"Revolt of the Mryaal", "The Goblin Plague",
                                           "Foray Into Fire", NULL};
    static const char *const modes[] = {"Super Easy", "Practice", "Normal", "Challenge",
                                        "Psycho Warrior", NULL};
    int sel;
    do {
        open_menu(episodes, 0x28, 0x12, default_pal() ? "{2}CHOOSE EPISODE" : "{3}CHOOSE EPISODE");
        const int ep = select_menu(episodes, 1);
        close_menu();
        if (ep > 3) {
            L.next_level = LEVEL_TITLE;
            return;
        }
        L.next_level = ep == 1 ? 0xFF : ep == 2 ? 7 : 0x10;
        open_menu(modes, 0x28, 0x12, default_pal() ? "{2}DIFFICULTY MODE" : "{3}DIFFICULTY MODE");
        sel = select_menu(modes, 2);
        close_menu();
    } while (sel == 100);
    G.diff_mode = (uint8_t)(sel - 1);
    Sess.diff_mode = G.diff_mode;
    game_init();
    G.quitflag = 7;
}

/* RXWINDF.C:368 CHECKPASSWORD(type): 0 the blood password to turn blood on or off, 1 the old one
 * (to change it), 2 a new one, 3 the new one again. 1 if right (or none is set), 0 if wrong, -1
 * for Esc. */
static int check_password(int type)
{
    static const int8_t xs[4] = {-1, 7, 5, 0};
    static const char *const titles[4] = {"Enter the Blood Password:", "Enter the old password:",
                                          "Enter the new password:",
                                          "Now re-enter the password:"};
    if (!(((type == 0 || type == 1) && Sess.blood_pass[0]) || type == 2 ||
          (type == 3 && U.blood_temp[0])))
        return 1;
    char title[48], s[20] = "";
    snprintf(title, sizeof title, "%s%s", default_pal() ? "{4}" : "{5}", titles[type]);
    win_up(-1, -1, type == 3 ? 0x1C : 0x1A, 6, default_pal() ? IMG_BORDER : IMG_BORDERT, title,
           NULL);
    win_get_str(s, xs[type], 2, 0x11, default_pal() ? 7 : 12, true);
    win_clear();
    if (s[0] == 1)
        return -1;
    if (type == 0 || type == 1)
        return strcmp(s, Sess.blood_pass) == 0;
    if (type == 2) {
        memcpy(U.blood_temp, s, sizeof U.blood_temp);
    } else {
        if (strcmp(s, (const char *)U.blood_temp))
            return 0;
        memcpy(Sess.blood_pass, U.blood_temp, sizeof Sess.blood_pass);
        Sess.blood_pass[19] = 0;
    }
    return 1;
}

/* The port's Enhancements menu (in OPTIONMENU): what the port adds to the original, each line
 * turning one on or off at once. The choices are remembered in ROCP.CFG (Enhance). */
static void enhance_menu(void)
{
    for (;;) {
        const bool stereo = al_stereo();
        const char *const menu[] = {host_crt() ? "CRT Filter: On" : "CRT Filter: Off",
                                    host_fullscreen() ? "Full Screen: On" : "Full Screen: Off",
                                    stereo ? "Music: Stereo" : "Music: Mono",
                                    game_title_scream ? "Title Scream: On" : "Title Scream: Off",
                                    NULL};
        open_menu(menu, 0x28, 0x10, default_pal() ? "{2}ENHANCEMENTS" : "{3}ENHANCEMENTS");
        const int sel = select_menu(menu, 5);
        close_menu();
        if (sel == 1) {
            Enhance.crt = !host_crt();
            host_set_crt(Enhance.crt);
        } else if (sel == 2) {
            host_set_fullscreen(!host_fullscreen());
            Enhance.fullscreen = host_fullscreen();
        } else if (sel == 3) {
            Enhance.stereo_music = !stereo;
            music_set_stereo(Enhance.stereo_music);
        } else if (sel == 4) {
            Enhance.title_scream = game_title_scream = !game_title_scream;
        } else {
            break;
        }
        enhance_save();
    }
}

/* RXWINDF.C:256 OPTIONMENU: the heroes' status (in a level), the hints, the status display,
 * blood (behind the blood password), the password, and the sound (not ported); and the port's
 * Enhancements. */
static void option_menu(void)
{
    const bool in_level = G.level < LEVEL_TITLE;
    for (;;) {
        const char *const messages = G.message_on ? "Messages: On" : "Messages: Off";
        const char *const display = G.status_on ? "Display: On" : "Display: Off";
        const char *const blood = G.blood_on ? "Blood: On" : "Blood: Off";
        const char *const level_menu[9] = {"Heroes' Status", messages, display, blood,
                                          "Blood Password", "Sound Volume", "Enhancements", NULL};
        const char *const title_menu[9] = {messages, display, blood, "Blood Password",
                                          "Sound Volume", "Enhancements", NULL};
        const char *const *menu = in_level ? level_menu : title_menu;
        open_menu(menu, 0x28, in_level ? 0x14 : 0x13,
                  default_pal() ? "{2}GAME OPTIONS" : "{3}GAME OPTIONS");
        const int sel = select_menu(menu, 3);
        close_menu();
        const int k = in_level ? sel - 1 : sel; /* 1 messages ... 5 sound, 6 enhancements */
        if (k == 1) {
            G.game_flags &= 0x0F;
            G.old_game_flags = 0;
            G.message_on = !G.message_on;
        } else if (k == 2) {
            G.status_on = !G.status_on;
        } else if (k == 3) {
            if (check_password(0) == 1)
                G.blood_on = !G.blood_on;
        } else if (k == 4) {
            if (check_password(1) == 1) {
                while (check_password(2) == 1 && check_password(3) == 0) {
                    win_up(-1, -1, 0x1E, 7, IMG_BORDER2, NULL, NULL);
                    const char *c = default_pal() ? "{4}" : "{5}";
                    char s[64];
                    snprintf(s, sizeof s, "%sSorry, but the two passwords", c);
                    menu_str(0x1E, 0x11, s);
                    snprintf(s, sizeof s, "%sdid not match! Try again.", c);
                    menu_str(0x1E, 0x1B, s);
                    window_cursor2(0xD8, 0x12, false);
                    win_clear();
                }
            }
        } else if (k == 5) {
            sound_menu();
        } else if (k == 6) {
            enhance_menu();
        } else if (in_level && sel == 1) {
            status_win(true);
        } else {
            break;
        }
        game_keep_session();
    }
    game_keep_session();
}

/* RXWINDF.C:72 MAINMENU: in a level, or on the title (where it has the Hall of Heroes and
 * no saving or leaving to the title). */
void main_menu(void)
{
    static const char *const level_menu[] = {"Play a New Game", "Game Options", "Load a Game",
                                             "Save a Game",     "Read Me!",     "Exit to Title",
                                             "Quit to DOS",     NULL};
    static const char *const title_menu[] = {"Play a New Game", "Game Options",
                                             "Load a Game",     "Hall of Heroes",
                                             "Read Me!",        "Quit to DOS", NULL};
    const char *header = default_pal() ? "{2}MAIN MENU" : "{3}MAIN MENU";
    for (;;) {
        const bool in_level = G.level < LEVEL_TITLE;
        const char *const *menu = in_level ? level_menu : title_menu;
        open_menu(menu, 0x28, in_level ? 0x14 : 0x15, header);
        const int sel = select_menu(menu, 0);
        close_menu();
        if (sel == 1) {
            difficulty_menu();
            if (G.quitflag)
                return;
        } else if (sel == 2) {
            option_menu();
            if (G.quitflag)
                return;
        } else if (sel == 3) {
            load_menu();
            if (G.quitflag)
                return;
        } else if (sel == 4) {
            if (!in_level) {
                G.quitflag = 5; /* the Hall of Heroes, on the text level */
                L.reader_file = NULL;
                return;
            }
            save_menu();
        } else if (sel == 5) {
            L.reader_file = "rocinst.txt";
            G.quitflag = 5;
            L.reader_page = 0;
            return;
        } else if (in_level && sel == 6) {
            if (prompt_win(PROMPT_END_GAME, NULL)) {
                G.quitflag = 2;
                return;
            }
        } else if (sel == 7 || (!in_level && sel == 6)) {
            if (prompt_win(PROMPT_QUIT, NULL)) {
                G.quitflag = 1;
                return;
            }
        } else {
            return;
        }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Saving and loading                                                                          */

/* RXWINDF.C:1509 LISTFILEDESC: the eight slots' descriptions. */
static void list_file_desc(char menu[SAVE_SLOTS][21])
{
    for (int i = 0; i < SAVE_SLOTS; i++)
        if (!save_describe(i + 1, menu[i]))
            snprintf(menu[i], 21, "-- Empty --");
}

/* RXWINDF.C:1304 SAVEMENU: pick a slot, type a description (an empty one is "* n *"). */
void save_menu(void)
{
    char names[SAVE_SLOTS][21];
    const char *menu[SAVE_SLOTS + 1];
    list_file_desc(names);
    for (int i = 0; i < SAVE_SLOTS; i++)
        menu[i] = names[i];
    menu[SAVE_SLOTS] = NULL;
    open_menu(menu, 0x28, 0x16, default_pal() ? "{2}SAVE A GAME" : "{3}SAVE A GAME");
    const int sel = select_menu(menu, 4);
    if (sel > SAVE_SLOTS) {
        close_menu();
        return;
    }
    char desc[24] = "", shown[32] = "";
    if (save_describe(sel, desc))
        snprintf(shown, sizeof shown, "{7}%s", desc);
    close_menu();
    win_up(-1, -1, 0x1A, 6, IMG_BORDER, "{4}Enter a file description:", NULL);
    font_write(0x0D, 0x0A, shown);
    win_get_str(desc, 5, 2, 0x13, 7, false);
    if (desc[0] != 1) {
        if (desc[0] == 0)
            snprintf(desc, sizeof desc, "* %c *", '0' + sel);
        if (save_game(sel, desc)) {
            L.quick_file = (uint8_t)sel;
        } else {
            win_clear();
            win_up(-1, -1, 0x16, 7, IMG_BORDER, "{4}FILE ERROR!", NULL);
            const char *s = "{4}The file was not saved!";
            font_write(0x58 - font_len(s) / 2, 0x10, s);
            window_cursor2(0x9A, 0x11, false);
        }
    }
    win_clear();
}

/* RXWINDF.C:1381 LOADMENU: pick a saved game (in a level, after a "restore?" prompt); the
 * restore happens as the level is set up again (_RestoreFlag, _quitflag 7). */
void load_menu(void)
{
    char names[SAVE_SLOTS][21];
    const char *menu[SAVE_SLOTS + 1];
    list_file_desc(names);
    for (int i = 0; i < SAVE_SLOTS; i++)
        menu[i] = names[i];
    menu[SAVE_SLOTS] = NULL;
    for (;;) {
        open_menu(menu, 0x28, 0x16, default_pal() ? "{2}LOAD A GAME" : "{3}LOAD A GAME");
        int sel;
        char desc[24];
        do {
            sel = select_menu(menu, 6);
            if (sel > SAVE_SLOTS) {
                close_menu();
                return;
            }
        } while (!save_describe(sel, desc));
        close_menu();
        if (G.level == LEVEL_TITLE || prompt_win(PROMPT_RESTORE, desc)) {
            L.quick_file = L.restore_flag = (uint8_t)sel;
            G.quitflag = 7;
            return;
        }
    }
}

/* WINDOWCURSOR2 for a "Press Y" window: the controller's attack button counts as Y. */
static int yes_cursor(int x, int y)
{
    host_pad_mode(PAD_PROMPT);
    const int k = window_cursor2(x, y, false);
    host_pad_mode(PAD_MENU);
    return k;
}

/* RXWINDF.C:1437 QUICKMENU(saving): F8 saves over the slot last used, F9 loads it, and after a
 * death (2) it offers to restore it if it holds this level; with no slot used yet, F8 / F9 open
 * the save / load menu. */
void quick_menu(int saving)
{
    if (L.quick_file == SAVE_NONE) {
        if (saving == 1)
            save_menu();
        else if (saving == 0)
            load_menu();
        return;
    }
    char desc[24];
    if (!save_describe(L.quick_file, desc))
        desc[0] = 0;
    const int level = save_level(L.quick_file);
    if (level != G.level && saving == 2)
        return;
    bool yes;
    if (saving == 2) {
        /* RXWINDF.C:1057 DEATHRESTOREWIN */
        win_up(-1, -1, 0x18, 0x0C, IMG_BORDER2, "{4}TRY AGAIN!", NULL);
        char s[64];
        menu_str(0x18, 0x10, "{4}Press \"Y\" if you");
        menu_str(0x18, 0x19, "{4}want to QuickRestore");
        snprintf(s, sizeof s, "{4}\"%s\"", desc);
        menu_str(0x18, 0x22, s);
        menu_str(0x18, 0x32, "{4}Press any other key");
        menu_str(0x18, 0x3B, "{4}to start this level over.");
        yes = yes_cursor(0xB6, 0x3E) == 0x15;
        win_clear();
    } else {
        yes = prompt_win(saving == 1 ? PROMPT_QUICKSAVE : PROMPT_QUICKRESTORE, desc);
    }
    if (!yes)
        return;
    if (saving == 1) {
        if (!save_game(L.quick_file, desc)) {
            win_up(-1, -1, 0x1F, 5, IMG_BORDER, NULL, NULL);
            font_write(0x0F, 0x10, "{4}File Error! The file was not saved.");
            window_cursor2(0xE1, 1, false);
        }
        win_clear();
    } else {
        L.restore_flag = L.quick_file;
        G.quitflag = 7;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Prompts and the pause window                                                                */

/* RXWINDF.C:900 PROMPTWIN(type, desc): "ARE YOU SURE?" ... Y says yes, any other key no. */
bool prompt_win(int type, const char *desc)
{
    const char *c = default_pal() ? "{4}" : "{5}";
    const bool file = type >= PROMPT_RESTORE;
    char s[96];
    snprintf(s, sizeof s, "%sARE YOU SURE?", c);
    win_up(-1, -1, 0x18, file ? 0x0C : 0x0B, IMG_BORDER2, s, NULL);
    snprintf(s, sizeof s, "%sPress \"Y\" if you really", c);
    menu_str(0x18, 0x10, s);
    snprintf(s, sizeof s, "%swant to %s", c, prompts[type]);
    menu_str(0x18, 0x19, s);
    if (file) {
        snprintf(s, sizeof s, "%s\"%s\"", c, desc ? desc : "");
        menu_str(0x18, 0x22, s);
    }
    snprintf(s, sizeof s, "%sPress any other key", c);
    menu_str(0x18, file ? 0x32 : 0x29, s);
    snprintf(s, sizeof s, "%s%s", c, go_backs[frandom(4)]);
    menu_str(0x18, file ? 0x3B : 0x32, s);
    const int k = yes_cursor(0xAC, file ? 0x3C : 0x34);
    win_clear();
    return k == 0x15; /* Y */
}

/* RXWINDF.C:961 STATUSWIN: the pause window (or the options' "Heroes' Status"): both heroes'
 * hit points (and poison), the level (episode, number and name, from rocdesc.txt), the gems
 * and the score. */
void status_win(bool from_menu)
{
    win_up(-1, -1, 0x1D, 10, IMG_BORDER2, NULL, NULL);
    char desc[31] = "";
    size_t len;
    uint8_t *d = Arc ? arc_load(Arc, "rocdesc.txt", &len) : NULL;
    if (d && (size_t)G.level * 30 + 30 <= len)
        snprintf(desc, sizeof desc, "%.30s", (const char *)d + G.level * 30);
    free(d);
    if (!from_menu) {
        music_pause();
        G.pause_hit = 0;
        G.main_pausing = 1;
    }
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
    window_cursor2(0xD0, 0x30, !from_menu);
    if (!from_menu) {
        G.pause_hit = 0;
        G.main_pausing = 0;
        music_continue();
        win_down();
    } else {
        win_clear();
    }
}

/* RXWINDF.C:1963 CHEATER(n): 0 everything, 1 ten hit points for both, 2 at least 250 gems, 3
 * the strongest weapons, 4 healed, 5 invincibility on or off. */
void cheater(int cheat)
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
    window_cursor2(0xAE, y - 8, false);
    win_clear();
}

/* RXWINDF.C:2017 WARPMENU: type a level number (1-26, two characters at most); _quitflag 7 goes
 * there. */
void warp_menu(void)
{
    win_up(-1, -1, 0x14, 6, IMG_BORDER, "{4}Enter level (1-26):", NULL);
    char s[10] = "";
    win_get_str(s, 2, 2, 2, 7, false);
    win_down();
    if (s[0] == 1 || s[0] == 0)
        return;
    uint8_t v = (uint8_t)s[0];
    if (strlen(s) != 1)
        v = (uint8_t)(s[1] + (s[0] - '0') * 10);
    if ((uint8_t)(v - '0') < 0x1C && (uint8_t)(v - '0') != 0) { /* (0 would be level -1) */
        L.next_level = (uint8_t)(v - '0' - 2);
        G.lightning_flag = 0;
        G.quitflag = 7;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* The results                                                                                 */

/* "m:ss" (NUMSTR with 2 digits, a leading space made a 0). */
static void time_str(char *out, size_t n, unsigned m, unsigned s)
{
    snprintf(out, n, "%u:%02u", m, s);
}

/* A results line: the label at x 0x23, the value right-aligned at 0x11d. */
static void result_line(int y, const char *label, const char *value)
{
    char s[48];
    font_write(0x23, y, label);
    snprintf(s, sizeof s, "{7}%s", value);
    font_write(0x11D - font_len(s), y, s);
}

static void result_count(int y, const char *label, unsigned found, unsigned in_all)
{
    char v[24];
    snprintf(v, sizeof v, "%u/%u", found, in_all);
    result_line(y, label, v);
}

/* RXWINDF.C:2905 / 2888 HS_LOADLEVTIME / HS_SAVELEVTIME */
static void level_time(unsigned *m, unsigned *s, int level)
{
    hscore_t h;
    hs_read(&h);
    *m = level < 27 ? h.best_minute[level] : 99;
    *s = level < 27 ? h.best_second[level] : 59;
}

/* RXWINDF.C:1089 PERCENTWIN: "Level Cleared!", the time against the best (a new best is kept),
 * the gems, enemies, statues and secrets found of the level's; it adds them to the totals. */
void percent_win(void)
{
    bool new_time = false;
    U.font = IMG_BIGFONT;
    U.font_space = -1;
    win_up(-1, -1, 0x28, 0x16, IMG_BORDER2, "{2}Level Cleared!", NULL);
    char v[24];
    if (G.minute_in_level < 100)
        time_str(v, sizeof v, G.minute_in_level, G.second_in_level);
    else
        snprintf(v, sizeof v, "Whoa!");
    result_line(0x1E, "{2}Your Time:", v);
    unsigned om, os;
    level_time(&om, &os, G.level);
    if (om == 99 && os == 0x3B)
        snprintf(v, sizeof v, "None");
    else
        time_str(v, sizeof v, om, os);
    result_line(0x2E, "{2}Best Time:", v);
    if (G.minute_in_level < om || (G.minute_in_level == om && G.second_in_level < os)) {
        hscore_t h;
        hs_read(&h);
        if (G.level < 27) {
            h.best_minute[G.level] = G.minute_in_level;
            h.best_second[G.level] = G.second_in_level;
            hs_write(&h);
        }
        new_time = true;
    }
    result_count(0x4E, "{2}Gems:", G.gem_found, G.gem_in_level);
    if (G.enemy_in_level < G.enemy_found)
        G.enemy_found = G.enemy_in_level;
    result_count(0x5E, "{2}Enemies:", G.enemy_found, G.enemy_in_level);
    result_count(0x6E, "{2}Statues:", G.statue_found, G.statue_in_level);
    result_count(0x7E, "{2}Secrets:", G.secret_found, G.secret_in_level);
    L.second_total = (uint16_t)(L.second_total + G.second_in_level);
    if (L.second_total > 0x3B) {
        L.minute_total++;
        L.second_total = (uint16_t)(L.second_total - 0x3C);
    }
    for (L.minute_total = (uint16_t)(L.minute_total + G.minute_in_level); L.minute_total > 0x3B;
         L.minute_total = (uint16_t)(L.minute_total - 0x3C))
        L.hour_total++;
    L.gem_total = (uint16_t)(L.gem_total + G.gem_found);
    L.enemy_total = (uint16_t)(L.enemy_total + G.enemy_found);
    L.statue_total = (uint16_t)(L.statue_total + G.statue_found);
    L.secret_total = (uint16_t)(L.secret_total + G.secret_found);
    L.gem_final = (uint16_t)(L.gem_final + G.gem_in_level);
    L.enemy_final = (uint16_t)(L.enemy_final + G.enemy_in_level);
    L.statue_final = (uint16_t)(L.statue_final + G.statue_in_level);
    L.secret_final = (uint16_t)(L.secret_final + G.secret_in_level);
    U.font = IMG_ROCFONT;
    U.font_space = 0;
    if (new_time)
        menu_str(0x28, 0x41, "{6}NEW BEST TIME!");
    menu_str(0x28, 0x92, host_joystick_on() ? "{2}PRESS A BUTTON!" : "{2}PRESS ANY KEY!");
    window_cursor2(0xD2, 0x8A, false);
    wait_tics(2);
    win_clear();
}

/* RXWINDF.C:1216 FINALPERCENTWIN: the episode's totals. */
void final_percent_win(void)
{
    U.font = IMG_BIGFONT;
    U.font_space = -1;
    win_up(-1, -1, 0x28, 0x12, IMG_BORDER2, "{2}Final Results:", NULL);
    char v[24];
    if (L.hour_total == 0)
        time_str(v, sizeof v, L.minute_total, L.second_total);
    else
        snprintf(v, sizeof v, "%u:%02u:%02u", L.hour_total, L.minute_total, L.second_total);
    result_line(0x1E, "{2}Your Time:", v);
    result_count(0x2E, "{2}Gems:", L.gem_total, L.gem_final);
    result_count(0x3E, "{2}Enemies:", L.enemy_total, L.enemy_final);
    result_count(0x4E, "{2}Statues:", L.statue_total, L.statue_final);
    result_count(0x5E, "{2}Secrets:", L.secret_total, L.secret_final);
    U.font = IMG_ROCFONT;
    U.font_space = 0;
    menu_str(0x28, 0x77, host_joystick_on() ? "{2}PRESS A BUTTON!" : "{2}PRESS ANY KEY!");
    window_cursor2(0xD2, 0x6F, false);
    wait_tics(2);
    win_clear();
}

/* RXWINDF.C:2849 HS_GETNAME */
static void hs_get_name(char *name)
{
    win_up(-1, -1, 0x25, 10, IMG_BORDER, "{4}CONGRATULATIONS!!", NULL);
    menu_str(0x25, 8, "{4}You have earned a place among those");
    menu_str(0x25, 0x11, "{4}worthy of the Hall of Heroes!");
    menu_str(0x25, 0x1A, "{4}Please enter your name:");
    win_get_str(name, 7, 0x25, 0x14, 7, false);
    win_down();
    if ((unsigned char)name[0] < 2)
        snprintf(name, 24, "I forgot my name!");
}

/* RXWINDF.C:2787 HS_CHECKSCORES(episode): the score, the time (as good or better), enemies,
 * statues, gems and secrets (as many or more) go into the Hall of Heroes, under a name asked
 * for once. */
void hs_check_scores(int ep)
{
    hscore_t h;
    hs_read(&h);
    char name[24] = "";
    if (Inv.score >= h.score[ep]) {
        h.score[ep] = Inv.score;
        hs_get_name(name);
        memcpy(h.score_name[ep], name, 24);
    }
    if (L.second_total + L.minute_total * 0x3C + L.hour_total * 0x168 <=
        h.time_secs[ep] + h.time_mins[ep] * 0x3C + h.time_hour[ep] * 0x168) {
        h.time_hour[ep] = L.hour_total;
        h.time_mins[ep] = L.minute_total;
        h.time_secs[ep] = L.second_total;
        if (!name[0])
            hs_get_name(name);
        memcpy(h.time_name[ep], name, 24);
    }
    struct { uint16_t *best, total; char *who; } c[4] = {
        {&h.enemy[ep], L.enemy_total, h.enemy_name[ep]},
        {&h.statue[ep], L.statue_total, h.statue_name[ep]},
        {&h.gems[ep], L.gem_total, h.gems_name[ep]},
        {&h.secret[ep], L.secret_total, h.secret_name[ep]},
    };
    for (int i = 0; i < 4; i++) {
        if (*c[i].best <= c[i].total) {
            *c[i].best = c[i].total;
            if (!name[0])
                hs_get_name(name);
            memcpy(c[i].who, name, 24);
        }
    }
    hs_write(&h);
}

/* ------------------------------------------------------------------------------------------ */
/* The title's pages                                                                           */

/* The title's keys while the credits or the scores show (Esc / Enter / Space the main menu,
 * F3 load, F9 quick load, F10 quit): true if one opened something. */
static bool title_keys(void)
{
    if (G.keys[0x01] || G.keys[0x1C] || G.keys[0x39]) {
        win_clear();
        main_menu();
    } else if (G.keys[0x3D]) {
        win_clear();
        load_menu();
    } else if (G.keys[0x43]) {
        win_clear();
        quick_menu(0);
    } else if (G.keys[0x44]) {
        win_clear();
        if (prompt_win(PROMPT_QUIT, NULL))
            G.quitflag = 1;
    } else {
        return false;
    }
    return true;
}

static bool title_goes_on(void)
{
    return G.quitflag == 0 && L.next_level == LEVEL_TITLE && L.restore_flag == 0;
}

/* RXWINDF.C:2492 DISPLAYCREDITS: the credits on the title's second picture for 3000 ticks. */
void display_credits(void)
{
    static const char *const names[9] = {"{4}Keith Schuler", "{4}Frank Maddin", "{4}Jim Dose'",
                                         "{4}Bobby Prince", "{4}Cygnus Multimedia",
                                         "{4}Susan Singer", "{4}Dirk Jones", "{4}Erinn Day",
                                         "{4}Stephanie Peaden"};
    static const int name_y[9] = {0, 0x10, 0x20, 0x30, 0x40, 0x48, 0x50, 0x58, 0x60};
    static const char *const jobs[5] = {"{7}Programming", "{7}MACH Game System", "{7}Sound Code",
                                        "{7}Music & Sound FX", "{7}Art & Animation"};
    static const char *const notes[4] = {"{9}Realms of Chaos is a trademark",
                                         "{9}of Apogee Software", "{9}(c) 1995 Apogee Software, Ltd.",
                                         "{9}All Rights Reserved"};
    bool faded = false;
    uint8_t f11 = 0;
    unsigned t = 0;
    for (;;) {
        win_up(-1, -1, 0x26, 0x11, IMG_BORDER2, NULL, NULL);
        for (int i = 0; i < 9; i++)
            font_write(0x9B, name_y[i], names[i]);
        for (int i = 0; i < 5; i++)
            font_write(0x94 - font_len_in(IMG_ROCFONT, jobs[i]), i * 0x10, jobs[i]);
        for (int i = 0; i < 4; i++)
            font_write(0x98 - font_len_in(IMG_ROCFONT, notes[i]) / 2, 0x76 + i * 9, notes[i]);
        if (!faded) {
            fade_to(G.level_pal);
            faded = true;
        }
        bool reopened = false;
        while (t < 3000 && title_goes_on()) {
            t += (unsigned)host_frame();
            if (title_keys()) {
                reopened = true;
                break;
            }
            gamma_key(&f11);
        }
        if (reopened && title_goes_on())
            continue;
        if (title_goes_on()) {
            fade_out();
            win_clear();
        }
        return;
    }
}

/* RXWINDF.C:2700 SCOREPAGE: a page of the Hall of Heroes: the episodes' best scores and times,
 * enemies and gems, or statues and secrets. */
static void score_page(int page, const hscore_t *h)
{
    for (int x = 0; x < 3; x++) {
        char s[16];
        snprintf(s, sizeof s, "{9}E%d.", x + 1);
        font_write(0x24, x * 9 + 0x21, s);
        font_write(0x24, x * 9 + 0x5B, s);
    }
    static const char *const heads[3][2] = {{"{7}TOTAL SCORES", "{7}OVERALL TIMES"},
                                            {"{7}ENEMIES KILLED", "{7}MANA GEMS COLLECTED"},
                                            {"{7}STATUES SMASHED", "{7}SECRETS UNCOVERED"}};
    menu_str(0x28, 0x10, heads[page][0]);
    menu_str(0x28, 0x48, heads[page][1]);
    for (int x = 0; x < 3; x++) {
        const char *n1, *n2;
        char v1[24], v2[24], s[48];
        if (page == 0) {
            n1 = h->score_name[x];
            n2 = h->time_name[x];
            snprintf(v1, sizeof v1, "%lu", (unsigned long)h->score[x]);
            snprintf(v2, sizeof v2, "%u:%02u:%02u", h->time_hour[x], h->time_mins[x],
                     h->time_secs[x]);
        } else if (page == 1) {
            n1 = h->enemy_name[x];
            n2 = h->gems_name[x];
            snprintf(v1, sizeof v1, "%u", h->enemy[x]);
            snprintf(v2, sizeof v2, "%u", h->gems[x]);
        } else {
            n1 = h->statue_name[x];
            n2 = h->secret_name[x];
            snprintf(v1, sizeof v1, "%u", h->statue[x]);
            snprintf(v2, sizeof v2, "%u", h->secret[x]);
        }
        snprintf(s, sizeof s, "{9}%s", n1);
        font_write(0x44, x * 9 + 0x21, s);
        snprintf(s, sizeof s, "{9}%s", n2);
        font_write(0x44, x * 9 + 0x5B, s);
        snprintf(s, sizeof s, "{9}%s", v1);
        font_write(0x11C - font_len(s), x * 9 + 0x21, s);
        snprintf(s, sizeof s, "{9}%s", v2);
        font_write(0x11C - font_len(s), x * 9 + 0x5B, s);
    }
}

/* The key a joystick button presses: _KeyTrans[_MyBUTTONn]. The reader and the Hall of Heroes
 * take button 0's key (the jump key, as SETUP ships them) for Esc and button 1's (attack) for
 * Enter, with or without a joystick. */
static int button_key(int b)
{
    return G.key_trans[Sess.my_button[b] & 7] & 0x7F;
}

/* RXWINDF.C:2582 DISPLAYSCORES(by_user): the Hall of Heroes' three pages. Picked from the menu,
 * the keys turn them (Esc, or Enter on the last, leaves); on the title each shows for 1500
 * ticks, and after the last the title's first picture comes back. */
void display_scores(bool by_user)
{
    hscore_t h;
    hs_read(&h);
    int page = 0;
    uint8_t f11 = 0;
    for (;;) {
        U.font = IMG_BIGFONT;
        U.font_space = -1;
        win_up(-1, -1, 0x28, 0x14, IMG_BORDER2, "{4}Hall of Heroes", NULL);
        U.font = IMG_ROCFONT;
        U.font_space = 0;
        score_page(page, &h);
        if (by_user) {
            const int k = window_cursor2(0x10C, 0x7A, false);
            if (k == 0x47) {
                page = 0;
            } else if (k == 0x4F) {
                page = 2;
            } else if ((k == 0x4B || k == 0x49 || k == 0x4A || k == 0x0C || k == 0x48) && page) {
                page--;
            } else if ((k == 0x4D || k == 0x51 || k == 0x4E || k == 0x0D || k == 0x50 ||
                        k == 0x1C || k == 0x39 || k == button_key(1)) &&
                       page < 2) {
                page++;
            } else if (k == 1 || (k == 0x1C && page == 2) || k == button_key(0)) {
                fade_out();
                win_clear();
                return;
            }
            win_clear();
            continue;
        }
        fade_to(G.level_pal);
        unsigned t = 0;
        bool reopened = false;
        while (t < 0x5DC && title_goes_on()) {
            t += (unsigned)host_frame();
            if (title_keys()) {
                reopened = true;
                break;
            }
            gamma_key(&f11);
        }
        if (reopened && title_goes_on())
            continue;
        if (!title_goes_on())
            return;
        if (page > 1) {
            /* back to the title's picture (title.pal / title.bg) */
            game_title_picture("title");
            L.skip_fade = 0;
            L.restore_flag = RESTORE_RERUN;
            G.vine_y = 0xFFFF;
            G.vine_x = 0;
            G.hitagain = 0;
            L.next_level = 0x1A;
            win_down();
            return;
        }
        page++;
        fade_out();
        win_clear();
    }
}

/* RXTITLE.C:342 ENDWIN: "READY FOR MORE?" after an episode's story. */
bool end_win(void)
{
    game_render(); /* REFRESHPAGE: the picture without the reader */
    win_up(-1, -1, 0x18, 0x0B, IMG_BORDER2, "{4}READY FOR MORE?", NULL);
    menu_str(0x18, 0x10, "{4}Press \"Y\" if you'd like to");
    menu_str(0x18, 0x19, "{4}launch into the next episode!");
    menu_str(0x18, 0x29, "{4}Press any other key to");
    menu_str(0x18, 0x32, "{4}return to the title.");
    fade_to(G.level_pal);
    const int k = yes_cursor(0xAC, 0x34);
    fade_out();
    win_clear();
    return k == 0x15;
}

/* ------------------------------------------------------------------------------------------ */
/* The page reader                                                                             */

static struct {
    uint8_t *file;
    const char *header[100], *body[100];
    int pages;
} R;

/* RXWINDF.C:1704 READERWIN: the page in a 25 x 39 window, the title on top and "Page n of m"
 * with the keys underneath; {Txxx} moves to column xxx, {Pxxxyyynn} puts a picture (InstIllID[nn],
 * or CollectID[nn - 10]) at (xxx, yyy); the text is centred on its widest line. */
static void reader_win(const char *header, const char *foot, const char *text)
{
    U.font_space = -1;
    win_up(-1, -1, 0x27, 0x19, IMG_BORDER2, header, foot);
    U.font_space = 0;
    struct { int x, y; char s[128]; } line[25];
    int n = 0, len = 0, ypos = 0;
    line[0].x = line[0].y = 0;
    for (const char *ch = text;; ch++) {
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
}

/* RXWINDF.C:1773 READERFETCHER: keys until the page changes (a CS table at 2ff5:3861): Esc out,
 * Home / End the first / last page, - Up PgUp Left keypad-minus back, = Enter Space Right
 * keypad-plus Down PgDn on (Enter on the last page leaves; button 0's key is Esc and button 1's
 * Enter). rocinst.txt wraps round, the stories don't. */
static void reader_fetcher(bool wraps)
{
    const int old = L.reader_page;
    while (L.reader_page == old) {
        int k = window_cursor2(0x124, 0xA1, false);
        if ((k == 0x1C && L.reader_page == R.pages - 1) || k == button_key(0))
            k = 1;
        else if (k == button_key(1))
            k = 0x1C;
        switch (k) {
        case 0x01: L.reader_page = 0x63; break;
        case 0x47: L.reader_page = 0; break;
        case 0x4F: L.reader_page = (int8_t)(R.pages - 1); break;
        case 0x0C: case 0x48: case 0x49: case 0x4A: case 0x4B:
            if (L.reader_page > 0)
                L.reader_page--;
            else if (wraps)
                L.reader_page = (int8_t)(R.pages - 1);
            break;
        case 0x0D: case 0x1C: case 0x39: case 0x4D: case 0x4E: case 0x50: case 0x51:
            if (L.reader_page < R.pages - 1)
                L.reader_page++;
            else if (wraps)
                L.reader_page = 0;
            break;
        default: break;
        }
    }
}

/* RXWINDF.C:1640 PAGEREADER(file): the first call loads the file (pairs of NUL-ended title and
 * text; the stories start at page 0, rocinst.txt where _ReaderPage says); each call shows a page
 * and waits until a key turns it; Esc ends it (_ReaderDone 2, the screen faded out). */
void page_reader(const char *file)
{
    const bool inst = !strcmp(file, "rocinst.txt");
    if (L.reader_done == 0) {
        L.reader_done = 1;
        free(R.file);
        size_t size = 0;
        R.file = Arc ? arc_load(Arc, file, &size) : NULL;
        if (!inst)
            L.reader_page = 0;
        R.pages = 0;
        size_t p = 0;
        while (R.file && p < size && R.pages < 100) {
            const size_t hl = strnlen((const char *)R.file + p, size - p);
            if (p + hl >= size)
                break;
            const size_t bl = strnlen((const char *)R.file + p + hl + 1, size - p - hl - 1);
            if (p + hl + 1 + bl >= size)
                break;
            R.header[R.pages] = (const char *)R.file + p;
            R.body[R.pages] = (const char *)R.file + p + hl + 1;
            R.pages++;
            p += hl + 1 + bl + 1;
        }
        if (L.reader_page >= R.pages)
            L.reader_page = 0;
    }
    if (R.pages == 0) {
        L.reader_page = 0x63;
    } else {
        char foot[128];
        snprintf(foot, sizeof foot, "{6}Page %d of %d {9}", L.reader_page + 1, R.pages);
        if (L.reader_page > 0 || inst)
            strcat(foot, "PgUp ");
        if (L.reader_page < R.pages - 1 || inst)
            strcat(foot, "PgDn ");
        strcat(foot, "{8}(Press ESC to exit)");
        reader_win(R.header[L.reader_page], foot, R.body[L.reader_page]);
        reader_fetcher(inst);
    }
    if (L.reader_page > 0x62) {
        free(R.file);
        R.file = NULL;
        L.reader_done = 2;
        U.font_space = -1;
        fade_out();
    }
}
