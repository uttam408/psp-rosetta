/* fx.h — cosmetic effects (port-spec §10). Cheap, self-removing entities. */
#ifndef RT_FX_H
#define RT_FX_H

#include "../src/world.h"

/* one-shot animation that removes itself when the Spritemap completes */
Entity *fx_anim(const char *tex_id, double x, double y, double rate);

/* a debris part: random frame, ballistic motion, gravity, dies after `life` frames */
Entity *fx_part(const char *tex_id, double x, double y,
                double toss_angle, double toss_speed, int life);

/* rising "+N" kill marker. TODO: real text — currently a small tinted quad. */
Entity *fx_blurb(int amount, double x, double y);

#endif
