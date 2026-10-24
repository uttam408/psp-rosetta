#ifndef NFM_CALLOUT_H
#define NFM_CALLOUT_H
#include <stdint.h>
#include "../../nfm/mad.h"

/* The original's on-screen call-outs (xtGraphics.stat): "Checkpoint Missed!", "Wrong Way!", "Power low, perform
 * stunt!", "Bad Landing!", the stunt name with its adjective, "Power Up N%", "Car Fixed", "Checkpoint!" and the
 * wasted-opponent lines.  callout_tick runs once per 30 Hz physics tick after mad_drive; callout_get returns what
 * the last tick left on screen (row = 8 px debug-text row, 0xAABBGGRR text colour). */
typedef struct { char text[96]; int row; uint32_t col; } CalloutLine;

void callout_reset(void);
/* ana = drawn arrow angle (deg, 0 = ahead), racing = countdown over, ai_names[k] = name of opponent k+1 */
/* returns 1 on the tick a stunt is announced (play the powerup sound) */
int  callout_tick(Mad *m, CheckPoints *cp, int nai, float ana, int racing, const char *const *ai_names);
int  callout_get(CalloutLine *out, int max);

#endif
