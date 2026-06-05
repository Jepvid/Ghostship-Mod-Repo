#include "mod.h"
#include "port/events/Events.h"

#define ACT_CROUCHING   0x0C008220
#define ACT_JUMP        0x03000880
#define ACT_DOUBLE_JUMP 0x03000881
#define ACT_LONG_JUMP   0x03000888
#define ACT_SIDE_FLIP   0x01000887
#define ACT_BACKFLIP    0x01000883

#define CHARGE_FRAMES     90        // 3 seconds at 30fps to build charge
#define CHARGE_HOLD_FRAMES 150      // 5 seconds to use the charge before it expires
#define PARTICLE_SPARKLES 0x00000008
// Long jump: push horizontal, keep trajectory flat
#define BOOST_LJ_VERT   1.2f
#define BOOST_LJ_HORIZ  2.0f
// Backflip / side flip: rocket vertical, leave horizontal as-is
#define BOOST_FLIP_VERT 1.5f
// Regular / double jump: balanced
#define BOOST_JUMP_VERT  1.8f
#define BOOST_JUMP_HORIZ 1.0f

static struct MarioState* gMario        = NULL;
static int                gCrouchFrames = 0;
static int                gCharged      = 0;
static int                gChargeHold   = 0;
static int                gBoostPending = 0;
static u32                gPrevAction   = 0;

static ListenerID gSetActionListenerID;
static ListenerID gExecuteActionListenerID;
static ListenerID gFrameUpdateListenerID;
static ListenerID gLongJumpListenerID;
static ListenerID gBackflipListenerID;

// Long jump and backflip are handled by dedicated events; only these remain.
static int is_charged_jump(u32 action) {
    return action == ACT_JUMP
        || action == ACT_DOUBLE_JUMP
        || action == ACT_SIDE_FLIP;
}

static void on_player_set_action(IEvent* event) {
    PlayerSetAction* e = (PlayerSetAction*)event;
    gMario = e->m;
}

// Must use PlayerExecuteAction so particleFlags is set before the spawner reads it
static void on_execute_action(IEvent* event) {
    (void)event;
    if (gCharged && gMario != NULL)
        gMario->particleFlags |= PARTICLE_SPARKLES;
}

static void on_long_jump(IEvent* event) {
    if (!gCharged) return;
    PlayerLongJump* e = (PlayerLongJump*)event;
    e->m->vel[1]   *= BOOST_LJ_VERT;
    *e->forwardVel *= BOOST_LJ_HORIZ;
    gCharged      = 0;
    gChargeHold   = 0;
    gCrouchFrames = 0;
}

static void on_backflip(IEvent* event) {
    if (!gCharged) return;
    PlayerBackflip* e = (PlayerBackflip*)event;
    e->m->vel[1] *= BOOST_FLIP_VERT;
    gCharged      = 0;
    gChargeHold   = 0;
    gCrouchFrames = 0;
}

static void on_frame_update(IEvent* event) {
    (void)event;
    if (gMario == NULL) return;

    u32 action = gMario->action;

    // Consume charge on regular/double/side-flip jumps; long jump and backflip
    // are handled by their dedicated event listeners above.
    if (gCharged && action != gPrevAction && is_charged_jump(action)) {
        gBoostPending = 1;
        gCharged      = 0;
        gChargeHold   = 0;
        gCrouchFrames = 0;
    }

    if (gBoostPending) {
        if (gMario->vel[1] > 0.0f) {
            u32 a = gMario->action;
            if (a == ACT_SIDE_FLIP) {
                gMario->vel[1] *= BOOST_FLIP_VERT;
            } else {
                gMario->vel[1]     *= BOOST_JUMP_VERT;
                gMario->forwardVel *= BOOST_JUMP_HORIZ;
            }
            gBoostPending = 0;
        } else if (!is_charged_jump(action)) {
            gBoostPending = 0;
        }
    }

    // Build charge while crouching still
    if (action == ACT_CROUCHING) {
        if (gCrouchFrames < CHARGE_FRAMES)
            gCrouchFrames++;
        if (gCrouchFrames >= CHARGE_FRAMES && !gCharged) {
            gCharged    = 1;
            gChargeHold = CHARGE_HOLD_FRAMES;
        }
    } else if (!gCharged) {
        // Lost crouch before fully charged — reset progress
        gCrouchFrames = 0;
    }

    // Countdown only starts once the player stops crouching
    if (gCharged && action != ACT_CROUCHING) {
        if (gChargeHold > 0)
            gChargeHold--;
        else {
            gCharged      = 0;
            gCrouchFrames = 0;
        }
    }

    gPrevAction = action;
}

MOD_INIT() {
    gSetActionListenerID     = REGISTER_LISTENER(PlayerSetAction,     EVENT_PRIORITY_NORMAL, on_player_set_action);
    gExecuteActionListenerID = REGISTER_LISTENER(PlayerExecuteAction, EVENT_PRIORITY_NORMAL, on_execute_action);
    gFrameUpdateListenerID   = REGISTER_LISTENER(GameFrameUpdate,     EVENT_PRIORITY_NORMAL, on_frame_update);
    gLongJumpListenerID      = REGISTER_LISTENER(PlayerLongJump,      EVENT_PRIORITY_NORMAL, on_long_jump);
    gBackflipListenerID      = REGISTER_LISTENER(PlayerBackflip,      EVENT_PRIORITY_NORMAL, on_backflip);
}

MOD_EXIT() {
    UNREGISTER_LISTENER(PlayerSetAction,     gSetActionListenerID);
    UNREGISTER_LISTENER(PlayerExecuteAction, gExecuteActionListenerID);
    UNREGISTER_LISTENER(GameFrameUpdate,     gFrameUpdateListenerID);
    UNREGISTER_LISTENER(PlayerLongJump,      gLongJumpListenerID);
    UNREGISTER_LISTENER(PlayerBackflip,      gBackflipListenerID);
}
