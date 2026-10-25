/* The game's files on the host (the port's): names in any case, and where the game is when the
 * command line doesn't say.
 *
 * DOS doesn't care about case, so a copy of the game may have ROC.SCO or roc.sco, ROC.DAT or
 * roc.dat, whatever the original asked for. On Linux and macOS (which can be case-sensitive)
 * the port looks for a file in any case before making a new one. */
#ifndef ROC_FILES_H
#define ROC_FILES_H

#include <stdbool.h>
#include <stddef.h>

/* The path of the file `name` in the folder `dir` ("" for the current folder): the name as it
 * is in the folder, in whatever case, or as given if it isn't there. */
void file_path(char *out, size_t n, const char *dir, const char *name);

/* ROC.EXE (with ROC.DAT beside it) when the command line names none, as when Steam or a desktop
 * launcher starts the game: next to the AppImage or the program, in a game folder there, in the
 * current folder, or in the port's own folder (~/.local/share/realms-of-chaos on Linux). When
 * it isn't in any of them, a message box lists them and it returns false. */
bool find_game(char *out, size_t n);

#endif
