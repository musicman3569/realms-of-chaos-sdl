Realms of Chaos for the Miyoo Mini (Plus) under OnionUI
=======================================================

A port of Apogee's 1995 platformer, reimplemented in C from the original's machine code. It holds
none of the game's files: it reads them from your copy of the game.

Installing
----------
1. Copy ROC.EXE and ROC.DAT (registered version 1.00) into this folder,
   Roms/PORTS/Games/Realms of Chaos (or into a folder named "game" inside it).
2. On the Miyoo, open Ports and run "~Import ports": "Realms of Chaos" then appears under
   Platform. (Ports must be turned on in Apps > Package Manager > Verified.)

The settings (ROC.CFG, ROCP.CFG), the Hall of Heroes (ROC.SCO) and the saved games (ROC.SG0-7 in
the original's format, which the DOS game can load, and the port's ROCP.SG0-7) are kept in the
same folder as ROC.EXE.

Controls
--------
                 In a level                          Menus and windows
D-pad            walk, climb, crouch                 move
B                jump (hold for height)              back (Esc)
Y                attack: whip / fireballs            pick
A                the special item (change hero)      pick (Enter)
X                look (with up / down)
L1 / R1          look up / down                      the help: page back / on
Start            the main menu (save, load, quit)    back (Esc)
Select           the status window (pause)
L2 / R2          quick save / quick load
Menu             OnionUI's game switcher

At a "Press Y" question, X or Y answers yes. B, Y, A and X stand where the A, X, B and Y of an
Xbox pad are, so they do what those do in the PC version.

To leave the game, pick "Quit to DOS" in the main menu (Start).

The Enhancements menu (Start > Game Options > Enhancements) has the music in stereo or mono and
the title's scream. The PC version's CRT filter isn't in this build.
