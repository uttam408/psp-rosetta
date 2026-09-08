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

double ent_hspeed(const Entity *e) { return e->Hspeed; }
double ent_vspeed(const Entity *e) { return e->Vspeed; }

/* AS3: set hspeed { direction = FP.angle(0,0,v,vspeed); Hspeed = v; } */
void ent_set_hspeed(Entity *e, double v)
{
    e->direction = fp_angle(0, 0, v, e->Vspeed);
    e->Hspeed = v;
}
void ent_set_vspeed(Entity *e, double v)
{
    e->direction = fp_angle(0, 0, e->Hspeed, v);
    e->Vspeed = v;
}

double ent_speed(const Entity *e) { return fp_distance(0, 0, e->Hspeed, e->Vspeed); }

/* AS3: set speed { angleXY(p, direction, s); hspeed = p.x; vspeed = p.y; } */
void ent_set_speed(Entity *e, double s)
{
    fp_vec p = {0, 0};
    fp_angle_xy(&p, e->direction, s, 0, 0);
    ent_set_hspeed(e, p.x);
    ent_set_vspeed(e, p.y);
}

void ent_motion_add(Entity *e, double angle_deg, double amount)
{
    fp_vec p = {0, 0};
    fp_angle_xy(&p, angle_deg, amount, 0, 0);
    ent_set_hspeed(e, e->Hspeed + p.x);
    ent_set_vspeed(e, e->Vspeed + p.y);
}

/* AS3 moveBy (no collision path): accumulate, round to int, apply. */
void ent_move_by(Entity *e, double dx, double dy)
{
    e->_moveX += dx;
    e->_moveY += dy;
    double rx = fp_round(e->_moveX);
    double ry = fp_round(e->_moveY);
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
    ent_set_vspeed(e, e->Vspeed + e->gravity);
    ent_set_speed(e, fp_approach(ent_speed(e), 0, e->friction));
}

bool ent_overlap(const Entity *a, double ax, double ay, const Entity *b)
{
    double al = ax - a->hb_ox, at = ay - a->hb_oy;
    double bl = b->x - b->hb_ox, bt = b->y - b->hb_oy;
    return al < bl + b->hb_w && al + a->hb_w > bl &&
           at < bt + b->hb_h && at + a->hb_h > bt;
}
