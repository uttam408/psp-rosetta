/* text.h — bitmap text, replacing FlashPunk's Text class (api-surface §7).
 * 8x8 public-domain font baked into one texture at init. Coordinates are SCREEN
 * space (the HUD follows the camera, matching Game.render's camera.x + offset). */
#ifndef RT_TEXT_H
#define RT_TEXT_H

#include <stdint.h>
#include "../src/real.h"

#define TEXT_LEFT   0
#define TEXT_CENTER 1
#define TEXT_RIGHT  2

void text_init(void);
void text_draw(const char *s, real sx, real sy, uint32_t rgb, int align);
void text_fill(real sx, real sy, real w, real h, uint32_t rgb);   /* solid screen-space rect */
int  text_width(const char *s);   /* pixels of the widest line */

#endif
