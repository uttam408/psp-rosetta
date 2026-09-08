#include "input.h"

static bool cur[ACT_COUNT];
static bool prev[ACT_COUNT];

void in_begin_frame(void)
{
    for (int i = 0; i < ACT_COUNT; i++) prev[i] = cur[i];
}
void in_set(Action a, bool down) { if (a < ACT_COUNT) cur[a] = down; }
bool in_down(Action a)     { return cur[a]; }
bool in_pressed(Action a)  { return cur[a] && !prev[a]; }
bool in_released(Action a) { return !cur[a] && prev[a]; }
