#include "entity.h"

void ent_init(Entity *e, EntityType t)
{
    *e = (Entity){0};
    e->type = t;
    e->collidable = true;
    e->alive = true;
    e->hb_w = e->hb_h = 1;
}

void ent_set_hitbox(Entity *e, int w, int h, int ox, int oy)
{
    e->hb_w = w; e->hb_h = h; e->hb_ox = ox; e->hb_oy = oy;
}

real ent_hspeed(const Entity *e) { return e->Hspeed; }
real ent_vspeed(const Entity *e) { return e->Vspeed; }

/* AS3: set hspeed { direction = FP.angle(0,0,v,vspeed); Hspeed = v; } */
void ent_set_hspeed(Entity *e, real v)
{
    e->direction = fp_angle(0, 0, v, e->Vspeed);
    e->Hspeed = v;
}
void ent_set_vspeed(Entity *e, real v)
{
    e->direction = fp_angle(0, 0, e->Hspeed, v);
    e->Vspeed = v;
}

real ent_speed(const Entity *e) { return fp_distance(0, 0, e->Hspeed, e->Vspeed); }

/* AS3: set speed { angleXY(p, direction, s); hspeed = p.x; vspeed = p.y; } */
void ent_set_speed(Entity *e, real s)
{
    fp_vec p = {0, 0};
    fp_angle_xy(&p, e->direction, s, 0, 0);
    ent_set_hspeed(e, p.x);
    ent_set_vspeed(e, p.y);
}

void ent_motion_add(Entity *e, real angle_deg, real amount)
{
    fp_vec p = {0, 0};
    fp_angle_xy(&p, angle_deg, amount, 0, 0);
    ent_set_hspeed(e, e->Hspeed + p.x);
    ent_set_vspeed(e, e->Vspeed + p.y);
}

/* AS3 moveBy (no collision path): accumulate, round to int, apply. */
void ent_move_by(Entity *e, real dx, real dy)
{
    e->_moveX += dx;
    e->_moveY += dy;
    real rx = fp_round(e->_moveX);
    real ry = fp_round(e->_moveY);
    e->_moveX -= rx;
    e->_moveY -= ry;
    e->x += rx;
    e->y += ry;
}

/* AS3 Entity.update():
 *   moveBy(Hspeed, Vspeed);
 *   this.vspeed += this.gravity;                 // calls the SETTER
 *   this.speed  = FP.approach(this.speed, 0, friction);
 */
void ent_update(Entity *e)
{
    ent_move_by(e, e->Hspeed, e->Vspeed);
    /* no gravity/friction: the speed round-trip below is an identity, and it
     * costs 3 atan2 + sin + cos per entity in soft-float on PSP. */
    if (e->gravity == 0 && e->friction == 0) return;
    ent_set_vspeed(e, e->Vspeed + e->gravity);
    ent_set_speed(e, fp_approach(ent_speed(e), 0, e->friction));
}

bool ent_overlap(const Entity *a, real ax, real ay, const Entity *b)
{
    real al = ax - a->hb_ox, at = ay - a->hb_oy;
    real bl = b->x - b->hb_ox, bt = b->y - b->hb_oy;
    return al < bl + b->hb_w && al + a->hb_w > bl &&
           at < bt + b->hb_h && at + a->hb_h > bt;
}
