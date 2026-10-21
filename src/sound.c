/* RXSAMPLF.C: the game's sounds on top of the sound devices (audio.c), and the levels' songs.
 *
 * The samples are rocsound.txt's items (LOADITEMS): 16-byte records of a VOC file's name, its
 * number (byte 14) and the number of the sample to play when it ends (byte 15: bats1 -> bats2 ->
 * bats3 -> bats1 make a loop). PLAYSAMPLE starts a sample and puts it in the sound list with
 * the object it comes from and a handle (_NextSoundListHandle); MAINTAINSOUND (called by the
 * level's ambient-noise object and the menus' cursor) drops the sounds that have ended and
 * starts their next samples under the same handle, if the object is in the foreground list or
 * on screen (or there's none), so SOUNDLINKACTIVE stays true while a chain plays. */
#include "engine.h"

#include "audio.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FX_PRIORITY = 7 };

typedef struct {
    char name[16];
    uint8_t link;                /* the sample after it, or 0 */
    bool known, tried;
    fx_sample_t s;
} item_t;

typedef struct snd snd_t;
struct snd {
    snd_t *next;
    uint8_t ident;               /* Link.priority: the list is sorted by it */
    obj_t *obj;                  /* objp: where it comes from, or NULL */
    bool obj_gone;               /* (the port's: the object was freed, or the level ended) */
    int voice;                   /* Handle (the sound device's) */
    int16_t hand_link;           /* HandLink: the handle the game holds */
};

static struct {
    const archive_t *arc;
    item_t item[256];
    snd_t *list;                 /* _SoundListHead */
    int16_t next_handle;         /* _NextSoundListHandle */
    bool on;                     /* _SoundCardType != -1 */
    int voices;                  /* _SoundVoices */
    char songs[29][14];          /* rocsong.txt */
} S = {.next_handle = 1};

int sound_voices(void)
{
    return S.voices;
}

/* LOADITEMS' sound part: rocsound.txt; and rocsong.txt's 13-character song names. */
void sound_init(const archive_t *arc, bool on, int voices)
{
    S.arc = arc;
    S.on = on;
    S.voices = voices;
    size_t len;
    uint8_t *d = arc_load(arc, "rocsound.txt", &len);
    for (size_t p = 0; d && p + 16 <= len; p += 16) {
        item_t *it = &S.item[d[p + 14]];
        memcpy(it->name, d + p, 14);
        it->name[14] = 0;
        it->link = d[p + 15];
        it->known = true;
    }
    free(d);
    d = arc_load(arc, "rocsong.txt", &len);
    for (int i = 0; d && i < 29 && (size_t)i * 13 + 13 <= len; i++) {
        int k = 0;
        for (; k < 13 && d[i * 13 + k] > ' '; k++)
            S.songs[i][k] = (char)d[i * 13 + k];
        S.songs[i][k] = 0;
    }
    free(d);
}

/* GETITEM: the sample's sound, read from ROC.DAT the first time. */
static const fx_sample_t *sample(uint8_t ident)
{
    item_t *it = &S.item[ident];
    if (!it->known)
        return NULL;
    if (!it->tried) {
        it->tried = true;
        size_t len;
        uint8_t *voc = arc_load(S.arc, it->name, &len);
        if (!voc || !fx_load_voc(&it->s, voc, len))
            fprintf(stderr, "roc: can't read %s\n", it->name);
        free(voc);
    }
    return it->s.pcm ? &it->s : NULL;
}

/* ADDPLINKNODE, by the sample's number: before the first one with a number >= its own. */
static void link_sound(snd_t *s)
{
    snd_t **p = &S.list;
    while (*p && (*p)->ident < s->ident)
        p = &(*p)->next;
    s->next = *p;
    *p = s;
}

/* RXSAMPLF.C:135 PLAYSAMPLE(n, source): the handle, or -3 with no sound card, -1 if the sample
 * can't be had. */
int play_sample(uint8_t n, obj_t *source)
{
    if (!S.on)
        return -3;
    if (!S.item[n].known) {
        fprintf(stderr, "roc: no sample %u\n", n);
        return -1;
    }
    const fx_sample_t *fx = sample(n);
    if (!fx)
        return -1;
    snd_t *s = calloc(1, sizeof *s);
    if (!s)
        return -1;
    s->ident = n;
    s->obj = source;
    s->hand_link = S.next_handle;
    link_sound(s);
    if (++S.next_handle > 0x7FBC)
        S.next_handle = 1;
    s->voice = fx_play(fx, FX_PRIORITY);
    return s->hand_link;
}

/* RXSAMPLF.C:51 SOUNDLINKACTIVE */
bool sound_link_active(int16_t handle)
{
    for (const snd_t *s = S.list; s; s = s->next)
        if (s->hand_link == handle)
            return true;
    return false;
}

/* RXSAMPLF.C:25 SOUNDLINKKILL */
void sound_link_kill(int16_t handle)
{
    for (snd_t **p = &S.list; *p; p = &(*p)->next) {
        if ((*p)->hand_link == handle) {
            snd_t *s = *p;
            fx_stop(s->voice);
            *p = s->next;
            free(s);
            return;
        }
    }
}

/* RXSAMPLF.C:64 MAINTAINSOUND: the sounds that ended leave the list (at most 8 a call), and up
 * to 8 of them go on with their next sample under the same handle, if they come from nothing,
 * from the foreground list or from an object in view. */
void maintain_sound(void)
{
    uint8_t play[8];
    int16_t handle[8];
    obj_t *source[8];
    snd_t *kill[8];
    int count = 0, count2 = 0;
    for (snd_t *s = S.list; s; s = s->next) {
        if (fx_active(s->voice))
            continue;
        if (count < 8 && !s->obj_gone &&
            (!s->obj || s->obj->list == LIST_FORE || obj_visible(s->obj))) {
            play[count] = S.item[s->ident].link;
            handle[count] = s->hand_link;
            source[count] = s->obj;
            count++;
        }
        if (count2 < 8)
            kill[count2++] = s;
    }
    while (count2) {
        snd_t *k = kill[--count2];
        for (snd_t **p = &S.list; *p; p = &(*p)->next)
            if (*p == k) {
                *p = k->next;
                free(k);
                break;
            }
    }
    const int16_t keep = S.next_handle;
    while (count) {
        count--;
        S.next_handle = handle[count];
        if (play[count])
            play_sample(play[count], source[count]);
    }
    S.next_handle = keep;
}

/* An object is freed: its sounds play out but don't go on. */
void sound_forget_obj(const obj_t *o)
{
    for (snd_t *s = S.list; s; s = s->next)
        if (s->obj == o) {
            s->obj = NULL;
            s->obj_gone = true;
        }
}

/* A level ends: its sounds play out, and none goes on. */
void sound_level_end(void)
{
    for (snd_t *s = S.list; s; s = s->next) {
        s->obj = NULL;
        s->obj_gone = true;
    }
}

/* The menus' and the title's sounds (_MenuVOC: sample 3 when a menu line moves, 0xa9 when one
 * is picked, 0xb0 or 0xb1 on the title), straight to the sound device. */
int sound_fx(uint8_t n)
{
    if (!S.on)
        return -1;
    return fx_play(sample(n), FX_PRIORITY);
}

/* NEWLEVELSETUP's music: the playing song fades out (0xe8 ms), then the level's (rocsong.txt)
 * plays, looped. */
void sound_song(int level)
{
    if (music_playing()) {
        music_fade(0, 0xE8);
        while (music_fade_active() && music_playing())
            SDL_Delay(5);
        music_stop();
    }
    if (level < 0 || level >= 29 || !S.songs[level][0])
        return;
    size_t len;
    uint8_t *mid = arc_load(S.arc, S.songs[level], &len);
    if (mid) {
        music_set_volume(Sess.music_volume);
        music_play(mid, len);
    }
    free(mid);
}
