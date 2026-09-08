/* enemy.h — Enemy.as base. Subclasses embed `Enemy en;` as their first member so
 * an Entity* / user pointer round-trips to the Enemy and then to the subclass. */
#ifndef RT_ENEMY_H
#define RT_ENEMY_H

#include "../src/world.h"

typedef struct Enemy {
    Entity e;                 /* MUST be first member */
    int    health;
    int    score;             /* added on removal (Brit 10, Jet 20, ...)      */
    int    kill_slot;         /* index into g_game.kills[]                    */
    const char *die_snd;      /* sfx played on death                          */
    const char *part_tex;     /* debris texture id (BritPart, JetPart, ...)   */
    bool   dying;             /* health<=0, playing out death anim/sink       */
    void (*on_death)(struct Enemy *);   /* NULL -> enemy_air_die              */
} Enemy;

/* apply `dmg`; if it drops health<=0, trigger the death path (on_death or air). */
void enemy_hit(Enemy *en, int dmg);

/* air-unit death: SmallExplosion + parts + "+N" + score, then remove.
 * (Sea units override with a sink sequence — TODO.) */
void enemy_air_die(Enemy *en);

/* call when the entity actually leaves the world: score + gameHard += 0.3 */
void enemy_on_removed(Enemy *en);

#endif
