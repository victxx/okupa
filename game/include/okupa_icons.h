#ifndef OKUPA_ICONS_H
#define OKUPA_ICONS_H

#include <stdint.h>

/* The buttons' icons, small 3D objects drawn once by the renderer at twice
 * their size and shrunk, so their edges are smooth: a colour and a coverage
 * (0..4) per pixel. */
enum { OK_ICON_FEED, OK_ICON_PLAY, OK_ICON_PET, OK_ICON_SLEEP, OK_ICON_WAKE, OK_ICON_DRAW, OK_ICON_PHOTO, OK_ICON_MAIL,
       OK_ICON_DEV, OK_ICON_WEAR, OK_ICONS };
#define OK_ICON_SIZE 32

typedef struct {
    uint16_t colour[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];
    uint8_t cover[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];
} ok_icons;

/* Uses the renderer's buffers: call it before drawing a frame, not during. */
void ok_icons_build(ok_icons *icons);
/* Draws icon centred at cx, cy over what is there, at size/32 (16 or 32). */
void ok_icons_draw(const ok_icons *icons, int icon, uint16_t *frame, int cx, int cy, int size);

#endif
