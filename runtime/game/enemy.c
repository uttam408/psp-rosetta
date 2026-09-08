#include "enemy.h"
#include "game.h"
#include "fx.h"
#include "../src/audio.h"

static bool player_alive(void)
{
    return world_count_type(&g_game.world, ETYPE_PLAYER) > 0;
}

void enemy_on_removed(Enemy *en)
{
    if (!player_alive()) return;                 /* kills after death don't score */
    g_game.game_score += en->score;
    if (en->kill_slot >= 0 && en->kill_slot < 4) g_game.kills[en->kill_slot]++;
    g_game.game_hard += SPEC_DIFFICULTY_GAMEHARD_PER_KILL;   /* Enemy.as:22 */
}

void enemy_air_die(Enemy *en)
{
    if (en->dying) return;
    en->dying = true;
    Entity *e = &en->e;

    if (player_alive() && en->die_snd) snd_play(en->die_snd, 1.0f, 0.0f);
    fx_anim("image/interaction/smallexplosion/explosion", e->x, e->y, 0.5);
    fx_blurb(en->score, e->x, e->y);

    /* up to 5 debris parts (world cap on FX is not enforced yet — TODO) */
    if (en->part_tex) {
        for (int i = 0; i < 5; i++)
            fx_part(en->part_tex, e->x, e->y,
                    e->direction, ent_speed(e) / 2.0 + 1.0, (int)fp_rand(300) + 1);
    }

    enemy_on_removed(en);
    world_remove(e->world, e);
}

void enemy_hit(Enemy *en, int dmg)
{
    if (en->dying) return;
    en->health -= dmg;
    if (en->health <= 0) enemy_air_die(en);      /* TODO: sea units sink instead */
}
