#include "okupa_icons.h"

#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))

void ok_icons_draw(int icon, uint16_t *frame, int cx, int cy, int size)
{
    int x, y, step = size >= OK_ICON_SIZE ? 1 : 2;
    if (icon < 0 || icon >= OK_ICONS) return;
    size = OK_ICON_SIZE / step;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++) {
            int fx = cx - size / 2 + x, fy = cy - size / 2 + y, n = 0, r = 0, g = 0, b = 0, dx, dy;
            uint16_t d;
            if (fx < 0 || fy < 0 || fx >= 256 || fy >= 192) continue;
            /* At half size, each pixel is the average of four. */
            for (dy = 0; dy < step; dy++)
                for (dx = 0; dx < step; dx++) {
                    int k = (y * step + dy) * OK_ICON_SIZE + x * step + dx, a = ok_icon_cover[icon][k];
                    uint16_t c = ok_icon_colour[icon][k];
                    n += a;
                    r += (c & 31) * a, g += (c >> 5 & 31) * a, b += (c >> 10 & 31) * a;
                }
            if (!n) continue;
            d = frame[fy * 256 + fx];
            r = r / n, g = g / n, b = b / n;
            n /= step * step;
            frame[fy * 256 + fx] = RGB((r * n + (d & 31) * (4 - n)) / 4, (g * n + (d >> 5 & 31) * (4 - n)) / 4,
                                       (b * n + (d >> 10 & 31) * (4 - n)) / 4);
        }
}
