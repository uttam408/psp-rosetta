/* input.h — logical actions. Backends map keys / the PSP pad to these.
 * (docs/roadmap.md: "Logical input actions" seam.) */
#ifndef RT_INPUT_H
#define RT_INPUT_H

#include <stdbool.h>

typedef enum {
    ACT_THRUST, ACT_LEFT, ACT_RIGHT, ACT_FIRE, ACT_MUTE, ACT_START, ACT_QUIT,
    ACT_COUNT
} Action;

void in_begin_frame(void);          /* snapshot previous state for edge detect */
void in_set(Action a, bool down);   /* backend feeds raw state                 */
bool in_down(Action a);
bool in_pressed(Action a);           /* went down this frame                    */
bool in_released(Action a);

#endif
