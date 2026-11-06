#ifndef OKUPA_ICONS_H
#define OKUPA_ICONS_H

#include <stdint.h>

/* The buttons' icons: little low-poly 3D toys drawn once by the renderer,
 * 32x32 with soft edges: a colour and a coverage (0..4) per pixel. */
enum { OK_ICON_FEED, OK_ICON_PLAY, OK_ICON_PET, OK_ICON_SLEEP, OK_ICON_WAKE, OK_ICON_DRAW, OK_ICON_PHOTO, OK_ICON_MAIL,
       OK_ICON_DEV, OK_ICON_WEAR, OK_ICON_HOME, OK_ICON_MUSIC, OK_ICON_TALK, OK_ICON_GAMES,
       OK_ICON_FRIEND, OK_ICON_DREAM, OK_ICONS };
#define OK_ICON_SIZE 32

extern uint16_t ok_icon_colour[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];
extern uint8_t ok_icon_cover[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];

/* Draws them all; it uses the renderer, so call it between frames, once. */
void ok_icons_build(void);

/* Draws an icon centred at cx, cy over a 256x192 frame, 32 or 16 pixels wide. */
void ok_icons_draw(int icon, uint16_t *frame, int cx, int cy, int size);

#endif
