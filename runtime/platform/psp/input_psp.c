/* input_psp.c — sceCtrl -> logical actions.
 * Thrust: analog up / D-pad up / X.  Steer: analog X / D-pad L-R.
 * Fire: Square or Circle.  Start: Start.  Mute: Select.  Quit: never (callback). */
#include "../../src/input.h"
#include <pspctrl.h>

#define DEAD 40   /* analog deadzone around centre (128) */

void psp_input_poll(void)
{
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    unsigned b = pad.Buttons;
    int ax = (int)pad.Lx - 128;
    int ay = (int)pad.Ly - 128;

    in_begin_frame();
    in_set(ACT_THRUST, (b & PSP_CTRL_UP)    || ay < -DEAD || (b & PSP_CTRL_CROSS));
    in_set(ACT_LEFT,   (b & PSP_CTRL_LEFT)  || ax < -DEAD);
    in_set(ACT_RIGHT,  (b & PSP_CTRL_RIGHT) || ax >  DEAD);
    in_set(ACT_FIRE,   (b & PSP_CTRL_SQUARE) || (b & PSP_CTRL_CIRCLE));
    in_set(ACT_START,  (b & PSP_CTRL_START) != 0);
    in_set(ACT_MUTE,   (b & PSP_CTRL_SELECT) != 0);
}

void psp_input_init(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}
