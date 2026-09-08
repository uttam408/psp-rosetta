/* world.h — FlashPunk World: an update list, a layer-sorted render list, and
 * per-type iteration for collision. Deliberately small (see api-surface §4). */
#ifndef RT_WORLD_H
#define RT_WORLD_H

#include "entity.h"

#define WORLD_MAX 512

typedef struct World {
    Entity *ents[WORLD_MAX];
    int count;
    /* pending adds are applied at the top of world_update so add-during-update
       is safe (FlashPunk semantics). */
    Entity *pending[WORLD_MAX];
    int pending_count;
} World;

void    world_init(World *w);
Entity *world_add(World *w, Entity *e);
void    world_remove(World *w, Entity *e);   /* marks !alive; swept post-update */
void    world_update(World *w);
void    world_render(World *w);              /* layer descending, then by add order */

int      world_count_type(World *w, EntityType t);
Entity  *world_first_type(World *w, EntityType t);
/* FlashPunk World.furthestFromEntity: the live entity of type `t`, != from,
   whose centre is furthest from `from` (NULL if none) */
Entity  *world_furthest_type(World *w, EntityType t, const Entity *from);
/* returns the first live entity of type `t` whose hitbox overlaps (a placed at ax,ay) */
Entity  *world_collide(World *w, EntityType t, Entity *a, double ax, double ay);

#endif
