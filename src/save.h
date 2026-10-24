/* RXSAVEF.C and the Hall of Heroes file: the settings (ROC.CFG), the high scores (roc.sco) and
 * saved games.
 *
 * ROC.CFG and roc.sco are the original's files, read and written in its format (the port uses
 * the same keys and settings, and leaves what it doesn't use, such as the sound card, as it
 * found it). The port saves games in its own format, ROCP.SG0-7 (and ROCP.SGT for the game the
 * reader leaves, ROC.SGT in the original), with the original's header (an ident, a version, the
 * 20-character description, the level) and the port's whole state after it. It also reads and
 * writes the original's ROC.SG0-7 (SAVEGAME, RESTOREGAME / MAKEOBJECT): saving a slot writes
 * both files, so the DOS game can load what the port saved. */
#ifndef ROC_SAVE_H
#define ROC_SAVE_H

#include <stdbool.h>
#include <stdint.h>

enum { SAVE_SLOTS = 8, SAVE_NONE = 'd' };

/* Where ROC.CFG, roc.sco and the saved games are (the game's folder, by default); NULL for
 * none: nothing is read or written (the headless tests). */
void save_set_dir(const char *dir);

/* LOADCONFIGFILE / SAVECONFIGFILE: the keys, the status display, hints, blood, the blood
 * password and the gamma, into and out of Sess. */
bool config_load(void);
void config_save(void);

/* HSCORE (594 bytes): the best of each episode, and the best time of each level. */
typedef struct {
    char score_name[3][24];
    uint32_t score[3];
    char time_name[3][24];
    uint16_t time_hour[3], time_mins[3], time_secs[3];
    char enemy_name[3][24];
    uint16_t enemy[3];
    char statue_name[3][24];
    uint16_t statue[3];
    char gems_name[3][24];
    uint16_t gems[3];
    char secret_name[3][24];
    uint16_t secret[3];
    uint16_t best_minute[27], best_second[27];
} hscore_t;

/* HS_READFILE: roc.sco, or the defaults (rocscore.txt in ROC.DAT). HS_WRITEFILE. */
void hs_read(hscore_t *h);
void hs_write(const hscore_t *h);

/* Saved games: slots 1-8, or RESTORE_TEMP. Each slot is the newer of the port's ROCP.SGn and
 * the original's ROC.SGn, and saving writes both; save_load_tables reads the animator table the
 * original's files number their animators by from ROC.EXE. */
bool save_load_tables(const char *exe_path);
bool save_game(int slot, const char *desc);        /* SAVEGAME: false on a file error */
bool save_describe(int slot, char desc[21]);       /* the slot's description, if it holds a game */
int save_level(int slot);                          /* RETRIEVELEVEL: its level, or SAVE_NONE */
bool restore_game(int slot);                       /* RESTOREGAME, once the level is loaded */
void save_delete(int slot);

#endif
