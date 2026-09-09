#include "world.h"
#include <stdlib.h>
#include <string.h>

void world_init(World *w) { memset(w, 0, sizeof *w); }

void world_clear(World *w)
{
    for (int i = 0; i < w->count; i++)
        if (w->ents[i]->recycle) w->ents[i]->recycle(w->ents[i]);
    for (int i = 0; i < w->pending_count; i++)
        if (w->pending[i]->recycle) w->pending[i]->recycle(w->pending[i]);
    memset(w, 0, sizeof *w);
}

Entity *world_add(World *w, Entity *e)
{
    if (w->pending_count < WORLD_MAX) {
        e->world = w;
        e->alive = true;
        w->pending[w->pending_count++] = e;
    }
    return e;
}

void world_remove(World *w, Entity *e)
{
    (void)w;
    if (e) e->alive = false;
}

static void flush_pending(World *w)
{
    for (int i = 0; i < w->pending_count && w->count < WORLD_MAX; i++)
        w->ents[w->count++] = w->pending[i];
    w->pending_count = 0;
}

void world_update(World *w)
{
    flush_pending(w);
    for (int i = 0; i < w->count; i++) {
        Entity *e = w->ents[i];
        if (e->alive && e->update) e->update(e);
    }
    /* sweep dead, returning each to its pool exactly once */
    int n = 0;
    for (int i = 0; i < w->count; i++) {
        Entity *e = w->ents[i];
        if (e->alive) w->ents[n++] = e;
        else if (e->recycle) e->recycle(e);
    }
    w->count = n;
    fp_frame++;
}

void world_render(World *w)
{
    /* FlashPunk renders higher layer first (further back). Stable insertion sort. */
    for (int i = 1; i < w->count; i++) {
        Entity *key = w->ents[i];
        int j = i - 1;
        while (j >= 0 && w->ents[j]->layer < key->layer) {
            w->ents[j + 1] = w->ents[j];
            j--;
        }
        w->ents[j + 1] = key;
    }
    for (int i = 0; i < w->count; i++) {
        Entity *e = w->ents[i];
        if (e->alive && e->render) e->render(e);
    }
}

int world_count_type(World *w, EntityType t)
{
    int c = 0;
    for (int i = 0; i < w->count; i++)
        if (w->ents[i]->alive && w->ents[i]->type == t) c++;
    for (int i = 0; i < w->pending_count; i++)
        if (w->pending[i]->type == t) c++;
    return c;
}

Entity *world_first_type(World *w, EntityType t)
{
    for (int i = 0; i < w->count; i++)
        if (w->ents[i]->alive && w->ents[i]->type == t) return w->ents[i];
    return NULL;
}

Entity *world_furthest_type(World *w, EntityType t, const Entity *from)
{
    Entity *best = NULL;
    double bestd = -1;
    for (int i = 0; i < w->count; i++) {
        Entity *e = w->ents[i];
        if (!e->alive || e->type != t || e == from) continue;
        double dx = e->x - from->x, dy = e->y - from->y;
        double d = dx * dx + dy * dy;
        if (d > bestd) { bestd = d; best = e; }
    }
    return best;
}

Entity *world_collide(World *w, EntityType t, Entity *a, double ax, double ay)
{
    for (int i = 0; i < w->count; i++) {
        Entity *b = w->ents[i];
        if (b->alive && b != a && b->type == t && b->collidable &&
            ent_overlap(a, ax, ay, b))
            return b;
    }
    return NULL;
}
