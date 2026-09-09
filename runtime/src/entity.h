/* entity.h — FlashPunk Entity motion model + AABB-by-type collision.
 * The hspeed/vspeed/speed/direction coupling is the whole flight feel — see
 * net/flashpunk/Entity.as and docs/luftrauser-port-spec.md §"Entity motion model". */
#ifndef RT_ENTITY_H
#define RT_ENTITY_H

#include "fp.h"

typedef enum {
    ETYPE_NONE = 0, ETYPE_PLAYER, ETYPE_ENEMY, ETYPE_BULLET, ETYPE_EBULLET,
    ETYPE_FX, ETYPE_UBOOT, ETYPE_WATER, ETYPE_SPACE, ETYPE_CLOUD, ETYPE_COUNT
} EntityType;

typedef struct Entity Entity;
struct Entity {
    double x, y;
    double Hspeed, Vspeed;   /* backing fields; use the accessors below       */
    double direction;        /* degrees; kept in sync by the h/vspeed setters */
    double gravity, friction;
    double _moveX, _moveY;   /* sub-pixel accumulators for moveBy              */

    int  hb_w, hb_h, hb_ox, hb_oy;  /* hitbox size + origin                    */
    EntityType type;
    int  layer;              /* higher = drawn first (FlashPunk convention)    */
    bool collidable;
    bool alive;              /* cleared by world_remove, swept after update    */

    void (*update)(Entity *self);
    void (*render)(Entity *self);
    void (*recycle)(Entity *self);  /* world calls this once, after death sweep */
    void  *user;             /* owning game object                            */
    struct World *world;
};

void   ent_init(Entity *e, EntityType t);
void   ent_set_hitbox(Entity *e, int w, int h, int ox, int oy);

double ent_hspeed(const Entity *e);
double ent_vspeed(const Entity *e);
void   ent_set_hspeed(Entity *e, double v);
void   ent_set_vspeed(Entity *e, double v);
double ent_speed(const Entity *e);
void   ent_set_speed(Entity *e, double s);

void   ent_motion_add(Entity *e, double angle_deg, double amount);
void   ent_move_by(Entity *e, double dx, double dy);
void   ent_update(Entity *e);   /* moveBy + gravity + friction (Entity.update) */

bool   ent_overlap(const Entity *a, double ax, double ay, const Entity *b);

#endif
