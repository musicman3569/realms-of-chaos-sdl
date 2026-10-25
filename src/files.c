/* The game's files on the host (see files.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* opendir, strcasecmp */
#endif
#include "files.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

enum { MAX_PLACES = 6 };

/* dir/name, or "" if it doesn't fit */
static void join(char *out, size_t n, const char *dir, const char *name)
{
    const size_t len = strlen(dir), name_len = strlen(name);
    const size_t sep = len && dir[len - 1] != '/' && dir[len - 1] != '\\';
    if (len + sep + name_len >= n) {
        *out = '\0';
        return;
    }
    memcpy(out, dir, len);
    if (sep)
        out[len] = '/';
    memcpy(out + len + sep, name, name_len + 1);
}

static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

void file_path(char *out, size_t n, const char *dir, const char *name)
{
    join(out, n, dir, name);
#ifndef _WIN32
    if (exists(out))
        return;
    DIR *d = opendir(*dir ? dir : ".");
    if (!d)
        return;
    for (const struct dirent *e; (e = readdir(d)) != NULL;)
        if (!strcasecmp(e->d_name, name)) {
            join(out, n, dir, e->d_name);
            break;
        }
    closedir(d);
#endif
}

/* ROC.EXE and ROC.DAT both in `dir`: out is ROC.EXE's path. */
static bool game_in(char *out, size_t n, const char *dir)
{
    char dat[1100];
    file_path(out, n, dir, "ROC.EXE");
    file_path(dat, sizeof dat, dir, "ROC.DAT");
    return exists(out) && exists(dat);
}

bool find_game(char *out, size_t n)
{
    char places[MAX_PLACES][1024];
    int count = 0;
    /* the AppImage's folder (the program itself is inside it, in a folder that comes and goes) */
    const char *appimage = getenv("APPIMAGE");
    if (appimage) {
        const char *slash = strrchr(appimage, '/');
        char folder[1000];
        snprintf(folder, sizeof folder, "%.*s", slash ? (int)(slash - appimage) : 0,
                 slash ? appimage : ".");
        snprintf(places[count++], sizeof places[0], "%s", folder);
        join(places[count++], sizeof places[0], folder, "game");
    } else {
        char *base = SDL_GetBasePath();
        if (base) {
            snprintf(places[count++], sizeof places[0], "%s", base);
            join(places[count++], sizeof places[0], base, "game");
            SDL_free(base);
        }
    }
    places[count++][0] = '\0';   /* the current folder */
    char *pref = SDL_GetPrefPath("", "realms-of-chaos");
    if (pref) {
        snprintf(places[count++], sizeof places[0], "%s", pref);
        SDL_free(pref);
    }
    for (int i = 0; i < count; i++)
        if (game_in(out, n, places[i]))
            return true;

    char text[4096];
    int len = snprintf(text, sizeof text,
                       "Realms of Chaos needs your copy of the game: ROC.EXE and ROC.DAT, in one "
                       "of these folders:\n\n");
    for (int i = 0; i < count && len < (int)sizeof text; i++)
        len += snprintf(text + len, sizeof text - len, "%s\n",
                        places[i][0] ? places[i] : "the current folder");
    if (len < (int)sizeof text)
        snprintf(text + len, sizeof text - len,
                 "\nOr name it on the command line: roc path/to/ROC.EXE");
    fprintf(stderr, "roc: %s\n", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Realms of Chaos", text, NULL);
    return false;
}
