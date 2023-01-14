#pragma once
#include "State.h"
#include "Config.h"
#include "Log.h"
#include "Header.h"
#include "UtilsBuffer.h"
#include <Xinput.h> // for deadzones values (questionable)

class ChangedMask;
struct InputValue;

static void CALLBACK ImplRepeatTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time);
static int ImplReextend(int virtKeyCode, bool *outExtended);
static void ImplGenerateMouseMotionFinish();

class ChangedMask {
    UINT TouchedUsers = 0;
    UINT ChangedUsers = 0;
    bool ChangedMouseMotion = false;

public:
    void TouchUser(int index, ImplState &state) {
        int mask = (1 << index);
        if (!(TouchedUsers & mask)) {
            state.Mutex.lock();
            TouchedUsers |= mask;
        }
    }

    void ChangeUser(int index, ImplState &state) {
        int mask = (1 << index);
        if (!(ChangedUsers & mask)) {
            state.IncrementVersion();
            ChangedUsers |= mask;
        }
    }

    void ChangeMouseMotion() { ChangedMouseMotion = true; }

    ~ChangedMask() {
        while (TouchedUsers) {
            auto &user = G.Users[ImplNextUser(&TouchedUsers)];
            user.State.Mutex.unlock();
        }

        while (ChangedUsers) {
            auto &user = G.Users[ImplNextUser(&ChangedUsers)];
            user.SendEvent();
        }

        if (ChangedMouseMotion) {
            ImplGenerateMouseMotionFinish();
        }
    }
};

struct InputValue {
    bool Down;
    double Strength;
    int Time;

    InputValue(bool down, double strength, int time) : Down(down), Strength(strength), Time(time) {}
};

static void ImplHandleInputChange(ImplRawState &state, bool down, double strength) {
    state.Pressed = down;

    if (down) {
        state.Strength = strength;
    } else {
        state.Strength = 0;
    }
}

static bool ImplHandleButtonInputChange(ImplButtonState &state, bool down, ImplButtonState *exclusiveState = nullptr) {
    ImplHandleInputChange(state, down, 1);

    if (exclusiveState) {
        if (exclusiveState->State && down) {
            exclusiveState->State = false;
        } else if (!down && exclusiveState->Pressed) {
            exclusiveState->State = true;
        }
    }

    bool oldDown = state.State;
    state.State = down;
    return down != oldDown;
}

static bool ImplHandleTriggerInputChange(ImplTriggerState &state, bool down, double strength) {
    ImplHandleInputChange(state, down, strength);

    double oldValue = state.Value;
    double value = down ? state.Strength * state.Modifier : 0;
    state.Value = value;
    state.State = value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

    return value != oldValue;
}

static bool ImplHandleTriggerModifierChange(ImplTriggerState &state, bool down, double strength, ChangedMask *changes) {
    bool changed = false;
    bool oldDown = state.ModifierPressed;
    state.ModifierPressed = down;

    if (oldDown != down) {
        state.Modifier = down ? strength : 1.0;

        if (state.Pressed) {
            changed = ImplHandleTriggerInputChange(state, state.Pressed, state.Strength);
        }
    }
    return changed;
}

static void ImplResetRotate(ImplAxisState &axisState) {
    if (axisState.RotateFakePressed) {
        axisState.RotateFakePressed = false;
        axisState.Extent = 0;
    }
    axisState.RotateModifier = 1.0;

    axisState.Pos.PrevPressedForRotate = axisState.Pos.Pressed;
    axisState.Neg.PrevPressedForRotate = axisState.Neg.Pressed;
}

static bool ImplHandleAxisInputChange(ImplRawState &state, ImplRawState &otherState, ImplAxisState &axisState, ImplAxisState &otherAxisState,
                                      ImplAxesState &axesState, double sign, bool isRight, bool down, double strength) {
    double deadzone = isRight ? (double)XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 0x7fff : (double)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 0x7fff;

    ImplHandleInputChange(state, down, strength);

    if (axisState.Pos.Pressed != axisState.Pos.PrevPressedForRotate || axisState.Neg.Pressed != axisState.Neg.PrevPressedForRotate ||
        otherAxisState.Pos.Pressed != otherAxisState.Pos.PrevPressedForRotate || otherAxisState.Neg.Pressed != otherAxisState.Neg.PrevPressedForRotate) {
        ImplResetRotate(axisState);
        ImplResetRotate(otherAxisState);
    }

    double extent = down ? state.Strength * state.Modifier * sign : otherState.Pressed ? otherState.Strength * otherState.Modifier * -sign
                                                                                       : 0;
    axisState.Extent = extent;
    double otherExtent = otherAxisState.Extent;

    double value = axisState.Extent * axisState.RotateModifier;
    double otherValue = otherAxisState.Extent * otherAxisState.RotateModifier;

    if ((extent > deadzone || extent < -deadzone) && (otherExtent > deadzone || otherExtent < -deadzone)) {
        double norm = abs(extent * otherExtent) /
                      sqrt(extent * extent * otherValue * otherValue + value * value * otherExtent * otherExtent);
        value *= norm;
        otherValue *= norm;
    }

    double oldValue = axisState.Value;
    double oldOtherValue = otherAxisState.Value;

    axisState.Value = value;
    otherAxisState.Value = otherValue;

    axisState.State = value > deadzone ? 1 : value < -deadzone ? -1
                                                               : 0;
    otherAxisState.State = otherValue > deadzone ? 1 : otherValue < -deadzone ? -1
                                                                              : 0;

    return value != oldValue || otherValue != oldOtherValue;
}

static bool ImplUpdateAxisOnModifierChange(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                           bool isRight) {
    bool changed = false;
    if (axisState.Pos.Pressed) {
        changed |= ImplHandleAxisInputChange(axisState.Pos, axisState.Neg, axisState, otherAxisState, axesState,
                                             1.0, isRight, axisState.Pos.Pressed, axisState.Pos.Strength);
    }
    if (axisState.Neg.Pressed) {
        changed |= ImplHandleAxisInputChange(axisState.Neg, axisState.Pos, axisState, otherAxisState, axesState,
                                             -1.0, isRight, axisState.Neg.Pressed, axisState.Neg.Strength);
    }
    return changed;
}

static bool ImplHandleAxisModifierChange(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                         bool isRight, bool down, double strength, ChangedMask *changes) {
    bool changed = false;
    bool oldDown = axisState.Pos.ModifierPressed || axisState.Neg.ModifierPressed;
    axisState.Pos.ModifierPressed = axisState.Neg.ModifierPressed = down;

    if (oldDown != down) {
        axisState.Pos.Modifier = axisState.Neg.Modifier = down ? strength : 1;

        changed = ImplUpdateAxisOnModifierChange(axisState, otherAxisState, axesState, isRight);
    }
    return changed;
}

static bool ImplHandleAxisRotatorChange(ImplRawState &state, ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                        bool y1, bool y2, bool y3, bool y4, bool isRight,
                                        ImplInput &input, bool down, double delta, ChangedMask *changes) {
    bool changed = false;
    if (down && (otherAxisState.Pos.Pressed || otherAxisState.Neg.Pressed)) {
        if (!input.HasTimer()) {
            input.StartTimer(30, ImplRepeatTimerProc);
        }

        if (!axisState.Pos.Pressed && !axisState.Neg.Pressed && !axisState.RotateFakePressed) {
            // gotta choose something!
            axisState.RotateFakePressed = true;
            axisState.RotateModifier = 0;
            axisState.Extent = state.Modifier;
        }

        auto adjustModifierByDelta = [&delta](double &modifier, double sign) {
            double oldDelta = delta;
            double oldModifier = modifier;
            modifier += delta * sign;

            delta = 0;
            if ((modifier > 0) != (oldModifier > 0) && oldModifier != 0 && modifier != 0) {
                delta = abs(modifier);
                modifier = 0;
            } else if (modifier < -1) {
                delta = -1 - modifier;
                modifier = -1;
            } else if (modifier > 1) {
                delta = modifier - 1;
                modifier = 1;
            }

            if (delta >= oldDelta) {
                delta = 0; // something went wrong, avoid hang at least
            }
        };

        while (delta > 0) {
            double xSign = axesState.X.Extent > 0 ? 1.0 : -1.0;
            double ySign = axesState.Y.Extent > 0 ? 1.0 : -1.0;
            double xExtent = xSign * axesState.X.RotateModifier;
            double yExtent = ySign * axesState.Y.RotateModifier;

            if (xExtent == 1 && (yExtent < 1 || (yExtent == 1 && y1)) && (yExtent > 0 || (yExtent == 0 && (!y4 && !y1 && ySign > 0)))) {
                adjustModifierByDelta(axesState.Y.RotateModifier, ySign * (y1 ? -1.0 : 1.0));
            } else if (yExtent == 1 && (xExtent < 1 || (xExtent == 1 && !y1)) && (xExtent > 0 || (xExtent == 0 && ((y1 && y2 && xSign > 0) || y1 != y2)))) {
                adjustModifierByDelta(axesState.X.RotateModifier, xSign * (y1 ? 1.0 : -1.0));
            } else if (yExtent == 1 && (xExtent > -1 || (xExtent == -1 && !y2)) && (xExtent < 0 || (xExtent == 0 && (y1 && y2 && xSign < 0)))) {
                adjustModifierByDelta(axesState.X.RotateModifier, xSign * (y2 ? -1.0 : 1.0));
            } else if (xExtent == -1 && (yExtent < 1 || (yExtent == 1 && y2)) && (yExtent > 0 || (yExtent == 0 && ((!y2 && !y3 && ySign > 0) || y2 != y3)))) {
                adjustModifierByDelta(axesState.Y.RotateModifier, ySign * (y2 ? -1.0 : 1.0));
            } else if (xExtent == -1 && (yExtent > -1 || (yExtent == -1 && y3)) && (yExtent < 0 || (yExtent == 0 && (!y2 && !y3 && ySign < 0)))) {
                adjustModifierByDelta(axesState.Y.RotateModifier, ySign * (y3 ? 1.0 : -1.0));
            } else if (yExtent == -1 && (xExtent > -1 || (xExtent == -1 && !y3)) && (xExtent < 0 || (xExtent == 0 && ((y3 && y4 && xSign < 0) || y3 != y4)))) {
                adjustModifierByDelta(axesState.X.RotateModifier, xSign * (y3 ? -1.0 : 1.0));
            } else if (yExtent == -1 && (xExtent < 1 || (xExtent == 1 && !y4)) && (xExtent > 0 || (xExtent == 0 && (y3 && y4 && xSign > 0)))) {
                adjustModifierByDelta(axesState.X.RotateModifier, xSign * (y4 ? 1.0 : -1.0));
            } else if (xExtent == 1 && (yExtent > -1 || (yExtent == -1 && y4)) && (yExtent < 0 || (yExtent == 0 && ((!y4 && !y1 && ySign < 0) || y4 != y1)))) {
                adjustModifierByDelta(axesState.Y.RotateModifier, ySign * (y4 ? 1.0 : -1.0));
            } else {
                delta = 0;
            }
        }

        changed |= ImplUpdateAxisOnModifierChange(axisState, otherAxisState, axesState, isRight);
        changed |= ImplUpdateAxisOnModifierChange(otherAxisState, axisState, axesState, isRight);
    } else if (!down && input.HasTimer()) {
        input.EndTimer();
    }
    return changed;
}

static BufferList *ImplInputBuffers() {
    static BufferList *inputBuffers = GBufferLists.Get(sizeof(INPUT));
    return inputBuffers;
}

static void ImplSendInputDelayed(void *param) {
    // We delay SendInput calls to avoid recursive/early hook calls

    SendInput_Real(1, (INPUT *)param, sizeof(INPUT));
    ImplInputBuffers()->PutBack(param);
}

static void ImplGenerateKey(int key, bool down, int time) {
    bool extended;
    key = ImplReextend(key, &extended);
    int scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);

    INPUT *input = (INPUT *)ImplInputBuffers()->Take();
    input->type = INPUT_KEYBOARD;
    input->ki = {};
    input->ki.wVk = key;
    input->ki.wScan = scan;
    if (!down) {
        input->ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    if (extended) {
        input->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    input->ki.time = time;
    input->ki.dwExtraInfo = ExtraInfoOurInject;

    PostAppCallback(ImplSendInputDelayed, input);
}

static void ImplGenerateMouseButton(int flag, int time, int xflag = 0) {
    INPUT *input = (INPUT *)ImplInputBuffers()->Take();
    input->type = INPUT_MOUSE;
    input->mi = {};
    input->mi.dwFlags = flag;
    input->mi.mouseData = xflag;
    input->mi.time = time;
    input->mi.dwExtraInfo = ExtraInfoOurInject;

    PostAppCallback(ImplSendInputDelayed, input);
}

static void ImplGenerateMouseWheel(int flag, double strength, int time) {
    int data = (int)strength * WHEEL_DELTA;
    ImplGenerateMouseButton(flag, time, data);
}

static void ImplGenerateMouseMotion(int dx, int dy, int time, ChangedMask *changes) {
    G.Mouse.MotionChange.dx += dx;
    G.Mouse.MotionChange.dy += dy;
    G.Mouse.MotionChange.time = time;
    changes->ChangeMouseMotion();
}

static void ImplGenerateMouseMotionFinish() {
    INPUT *input = (INPUT *)ImplInputBuffers()->Take();
    input->type = INPUT_MOUSE;
    input->mi = {};
    input->mi.dwFlags = MOUSEEVENTF_MOVE;
    input->mi.dx = G.Mouse.MotionChange.dx;
    input->mi.dy = G.Mouse.MotionChange.dy;
    input->mi.time = G.Mouse.MotionChange.time;
    input->mi.dwExtraInfo = ExtraInfoOurInject;

    PostAppCallback(ImplSendInputDelayed, input);

    G.Mouse.MotionChange = {};
}

static void ImplProcessInputChange(ImplInput &input, InputValue &v, ChangedMask *changes) {
    if (!input.IsValid()) {
        return;
    }

    int key = input.DestKey;
    if (input.DestType.OfUser) {
        int userIndex = input.User;
        if (userIndex < 0) {
            userIndex = G.ActiveUser;
        }

        ImplUser *user = ImplGetUser(userIndex);
        if (!user) {
            return;
        }

        ImplState &state = user->State;
        changes->TouchUser(userIndex, state);

        bool changed = false;
        switch (key) {
        case MY_VK_PAD_A:
            changed = ImplHandleButtonInputChange(state.A, v.Down);
            break;
        case MY_VK_PAD_B:
            changed = ImplHandleButtonInputChange(state.B, v.Down);
            break;
        case MY_VK_PAD_X:
            changed = ImplHandleButtonInputChange(state.X, v.Down);
            break;
        case MY_VK_PAD_Y:
            changed = ImplHandleButtonInputChange(state.Y, v.Down);
            break;
        case MY_VK_PAD_START:
            changed = ImplHandleButtonInputChange(state.Start, v.Down);
            break;
        case MY_VK_PAD_BACK:
            changed = ImplHandleButtonInputChange(state.Back, v.Down);
            break;
        case MY_VK_PAD_DPAD_LEFT:
            changed = ImplHandleButtonInputChange(state.DL, v.Down, &state.DR);
            break;
        case MY_VK_PAD_DPAD_RIGHT:
            changed = ImplHandleButtonInputChange(state.DR, v.Down, &state.DL);
            break;
        case MY_VK_PAD_DPAD_UP:
            changed = ImplHandleButtonInputChange(state.DU, v.Down, &state.DD);
            break;
        case MY_VK_PAD_DPAD_DOWN:
            changed = ImplHandleButtonInputChange(state.DD, v.Down, &state.DU);
            break;
        case MY_VK_PAD_LSHOULDER:
            changed = ImplHandleButtonInputChange(state.LB, v.Down);
            break;
        case MY_VK_PAD_RSHOULDER:
            changed = ImplHandleButtonInputChange(state.RB, v.Down);
            break;
        case MY_VK_PAD_LTHUMB_PRESS:
            changed = ImplHandleButtonInputChange(state.L, v.Down);
            break;
        case MY_VK_PAD_RTHUMB_PRESS:
            changed = ImplHandleButtonInputChange(state.R, v.Down);
            break;
        case MY_VK_PAD_GUIDE:
            changed = ImplHandleButtonInputChange(state.Guide, v.Down);
            break;
        case MY_VK_PAD_EXTRA:
            changed = ImplHandleButtonInputChange(state.Extra, v.Down);
            break;

        case MY_VK_PAD_LTRIGGER:
            changed = ImplHandleTriggerInputChange(state.LT, v.Down, v.Strength);
            break;
        case MY_VK_PAD_RTRIGGER:
            changed = ImplHandleTriggerInputChange(state.RT, v.Down, v.Strength);
            break;

        case MY_VK_PAD_LTHUMB_UP:
            changed = ImplHandleAxisInputChange(state.LA.U, state.LA.D, state.LA.Y, state.LA.X, state.LA,
                                                1.0, false, v.Down, v.Strength);
            break;
        case MY_VK_PAD_LTHUMB_DOWN:
            changed = ImplHandleAxisInputChange(state.LA.D, state.LA.U, state.LA.Y, state.LA.X, state.LA,
                                                -1.0, false, v.Down, v.Strength);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT:
            changed = ImplHandleAxisInputChange(state.LA.R, state.LA.L, state.LA.X, state.LA.Y, state.LA,
                                                1.0, false, v.Down, v.Strength);
            break;
        case MY_VK_PAD_LTHUMB_LEFT:
            changed = ImplHandleAxisInputChange(state.LA.L, state.LA.R, state.LA.X, state.LA.Y, state.LA,
                                                -1.0, false, v.Down, v.Strength);
            break;
        case MY_VK_PAD_RTHUMB_UP:
            changed = ImplHandleAxisInputChange(state.RA.U, state.RA.D, state.RA.Y, state.RA.X, state.RA,
                                                1.0, true, v.Down, v.Strength);
            break;
        case MY_VK_PAD_RTHUMB_DOWN:
            changed = ImplHandleAxisInputChange(state.RA.D, state.RA.U, state.RA.Y, state.RA.X, state.RA,
                                                -1.0, true, v.Down, v.Strength);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT:
            changed = ImplHandleAxisInputChange(state.RA.R, state.RA.L, state.RA.X, state.RA.Y, state.RA,
                                                1.0, true, v.Down, v.Strength);
            break;
        case MY_VK_PAD_RTHUMB_LEFT:
            changed = ImplHandleAxisInputChange(state.RA.L, state.RA.R, state.RA.X, state.RA.Y, state.RA,
                                                -1.0, true, v.Down, v.Strength);
            break;

        case MY_VK_PAD_LTHUMB_HORZ_MODIFIER:
        case MY_VK_PAD_LTHUMB_VERT_MODIFIER:
        case MY_VK_PAD_LTHUMB_MODIFIER:
            if (key != MY_VK_PAD_LTHUMB_VERT_MODIFIER) {
                changed = ImplHandleAxisModifierChange(state.LA.X, state.LA.Y, state.LA, false, v.Down, v.Strength, changes);
            }
            if (key != MY_VK_PAD_LTHUMB_HORZ_MODIFIER) {
                changed = ImplHandleAxisModifierChange(state.LA.Y, state.LA.X, state.LA, false, v.Down, v.Strength, changes);
            }
            break;

        case MY_VK_PAD_RTHUMB_HORZ_MODIFIER:
        case MY_VK_PAD_RTHUMB_VERT_MODIFIER:
        case MY_VK_PAD_RTHUMB_MODIFIER:
            if (key != MY_VK_PAD_RTHUMB_VERT_MODIFIER) {
                changed = ImplHandleAxisModifierChange(state.RA.X, state.RA.Y, state.RA, true, v.Down, v.Strength, changes);
            }
            if (key != MY_VK_PAD_RTHUMB_HORZ_MODIFIER) {
                changed = ImplHandleAxisModifierChange(state.RA.Y, state.RA.X, state.RA, true, v.Down, v.Strength, changes);
            }
            break;

        case MY_VK_PAD_TRIGGER_MODIFIER:
        case MY_VK_PAD_LTRIGGER_MODIFIER:
        case MY_VK_PAD_RTRIGGER_MODIFIER:
            if (key != MY_VK_PAD_RTRIGGER_MODIFIER) {
                changed = ImplHandleTriggerModifierChange(state.LT, v.Down, v.Strength, changes);
            }
            if (key != MY_VK_PAD_LTRIGGER_MODIFIER) {
                changed = ImplHandleTriggerModifierChange(state.RT, v.Down, v.Strength, changes);
            }
            break;

        case MY_VK_PAD_LTHUMB_UP_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.U, state.LA.Y, state.LA.X, state.LA,
                                                  false, false, true, true,
                                                  false, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_LTHUMB_DOWN_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.D, state.LA.Y, state.LA.X, state.LA,
                                                  true, true, false, false,
                                                  false, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.R, state.LA.X, state.LA.Y, state.LA,
                                                  true, false, false, true,
                                                  false, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_LTHUMB_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.L, state.LA.X, state.LA.Y, state.LA,
                                                  false, true, true, false,
                                                  false, input, v.Down, v.Strength, changes);
            break;

        case MY_VK_PAD_RTHUMB_UP_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.U, state.RA.Y, state.RA.X, state.RA,
                                                  false, false, true, true,
                                                  true, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_RTHUMB_DOWN_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.D, state.RA.Y, state.RA.X, state.RA,
                                                  true, true, false, false,
                                                  true, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.R, state.RA.X, state.RA.Y, state.RA,
                                                  true, false, false, true,
                                                  true, input, v.Down, v.Strength, changes);
            break;
        case MY_VK_PAD_RTHUMB_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.L, state.RA.X, state.RA.Y, state.RA,
                                                  false, true, true, false,
                                                  true, input, v.Down, v.Strength, changes);
            break;

        case MY_VK_SET_ACTIVE_USER:
            if (!v.Down) {
                if (G.DefaultActiveUser == G.ActiveUser) {
                    G.ActiveUser = userIndex;
                }
                G.DefaultActiveUser = userIndex;
            }
            break;

        case MY_VK_HOLD_ACTIVE_USER:
            if (v.Down) {
                G.ActiveUser = userIndex;
            } else {
                G.ActiveUser = G.DefaultActiveUser;
            }
            break;

        default:
            Fatal("Invalid pad input action?!");
        }

        if (changed) {
            changes->ChangeUser(userIndex, state);
        }
    } else {
        switch (key) {
        case MY_VK_RELOAD:
            if (!v.Down) {
                ConfigReload();
            }
            break;

        case MY_VK_TOGGLE_DISABLE:
            if (!v.Down) {
                G.Disable = !G.Disable;
                UpdateAll(); // e.g. may change mouse capture
            }
            break;

        case VK_LBUTTON:
            ImplGenerateMouseButton(v.Down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, v.Time);
            break;
        case VK_RBUTTON:
            ImplGenerateMouseButton(v.Down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, v.Time);
            break;
        case VK_MBUTTON:
            ImplGenerateMouseButton(v.Down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, v.Time);
            break;
        case VK_XBUTTON1:
            ImplGenerateMouseButton(v.Down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, v.Time, XBUTTON1);
            break;
        case VK_XBUTTON2:
            ImplGenerateMouseButton(v.Down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, v.Time, XBUTTON2);
            break;

        case MY_VK_WHEEL_DOWN:
            ImplGenerateMouseWheel(MOUSEEVENTF_WHEEL, -v.Strength, v.Time);
            break;
        case MY_VK_WHEEL_UP:
            ImplGenerateMouseWheel(MOUSEEVENTF_WHEEL, v.Strength, v.Time);
            break;
        case MY_VK_WHEEL_LEFT:
            ImplGenerateMouseWheel(MOUSEEVENTF_HWHEEL, -v.Strength, v.Time);
            break;
        case MY_VK_WHEEL_RIGHT:
            ImplGenerateMouseWheel(MOUSEEVENTF_HWHEEL, v.Strength, v.Time);
            break;

        case MY_VK_MOUSE_DOWN:
            ImplGenerateMouseMotion(0, (int)v.Strength, v.Time, changes);
            break;
        case MY_VK_MOUSE_UP:
            ImplGenerateMouseMotion(0, (int)-v.Strength, v.Time, changes);
            break;
        case MY_VK_MOUSE_LEFT:
            ImplGenerateMouseMotion((int)-v.Strength, 0, v.Time, changes);
            break;
        case MY_VK_MOUSE_RIGHT:
            ImplGenerateMouseMotion((int)v.Strength, 0, v.Time, changes);
            break;

        default:
            if (key > 0 && key < MY_VK_LAST_REAL) {
                ImplGenerateKey(key, v.Down, v.Time);
            } else {
                Fatal("Invalid input action?!");
            }
            break;
        }
    }
}

static int ImplUnextend(int virtKeyCode, bool extended) {
    switch (virtKeyCode) {
    case VK_INSERT:
        return extended ? virtKeyCode : VK_NUMPAD0;
    case VK_END:
        return extended ? virtKeyCode : VK_NUMPAD1;
    case VK_DOWN:
        return extended ? virtKeyCode : VK_NUMPAD2;
    case VK_NEXT:
        return extended ? virtKeyCode : VK_NUMPAD3;
    case VK_LEFT:
        return extended ? virtKeyCode : VK_NUMPAD4;
    case VK_CLEAR:
        return extended ? virtKeyCode : VK_NUMPAD5;
    case VK_RIGHT:
        return extended ? virtKeyCode : VK_NUMPAD6;
    case VK_HOME:
        return extended ? virtKeyCode : VK_NUMPAD7;
    case VK_UP:
        return extended ? virtKeyCode : VK_NUMPAD8;
    case VK_PRIOR:
        return extended ? virtKeyCode : VK_NUMPAD9;
    case VK_DELETE:
        return extended ? virtKeyCode : VK_DECIMAL;
    case VK_RETURN:
        return extended ? MY_VK_NUMPAD_RETURN : virtKeyCode;
    case VK_CANCEL:
        return extended ? VK_PAUSE : VK_SCROLL;
    default:
        return virtKeyCode;
    }
}

// TODO: probably wrong in some scenarios - esp. GetKeyState?
static bool IsControlOn() { return GetAsyncKeyState(VK_CONTROL) < 0; }
static bool IsNumpadOn() { return (GetKeyState(VK_NUMLOCK) & 1) != 0; }

static int ImplReextend(int virtKeyCode, bool *outExtended) {
    *outExtended = false;
    switch (virtKeyCode) {
    case VK_INSERT:
    case VK_END:
    case VK_DOWN:
    case VK_NEXT:
    case VK_LEFT:
    case VK_CLEAR:
    case VK_RIGHT:
    case VK_HOME:
    case VK_UP:
    case VK_PRIOR:
    case VK_DELETE:
    case VK_DIVIDE:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_SNAPSHOT:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    // not sure if applies to all of these:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_MEDIA_STOP:
    case VK_VOLUME_UP:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_MUTE:
    case VK_BROWSER_HOME:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_STOP:
    case VK_BROWSER_SEARCH:
    case VK_BROWSER_FAVORITES:
    case VK_LAUNCH_MAIL:
    case VK_LAUNCH_MEDIA_SELECT:
    case VK_LAUNCH_APP1:
    case VK_LAUNCH_APP2:
        *outExtended = true;
        return virtKeyCode;

    case MY_VK_NUMPAD_RETURN:
        *outExtended = true;
        return VK_RETURN;

    case VK_NUMPAD0:
        return IsNumpadOn() ? virtKeyCode : VK_INSERT;
    case VK_NUMPAD1:
        return IsNumpadOn() ? virtKeyCode : VK_END;
    case VK_NUMPAD2:
        return IsNumpadOn() ? virtKeyCode : VK_DOWN;
    case VK_NUMPAD3:
        return IsNumpadOn() ? virtKeyCode : VK_NEXT;
    case VK_NUMPAD4:
        return IsNumpadOn() ? virtKeyCode : VK_LEFT;
    case VK_NUMPAD5:
        return IsNumpadOn() ? virtKeyCode : VK_CLEAR;
    case VK_NUMPAD6:
        return IsNumpadOn() ? virtKeyCode : VK_RIGHT;
    case VK_NUMPAD7:
        return IsNumpadOn() ? virtKeyCode : VK_HOME;
    case VK_NUMPAD8:
        return IsNumpadOn() ? virtKeyCode : VK_UP;
    case VK_NUMPAD9:
        return IsNumpadOn() ? virtKeyCode : VK_PRIOR;
    case VK_DECIMAL:
        return IsNumpadOn() ? virtKeyCode : VK_DELETE;

    case VK_SCROLL:
        return IsControlOn() ? VK_CANCEL : virtKeyCode;
    case VK_PAUSE:
        *outExtended = IsControlOn();
        return *outExtended ? VK_CANCEL : virtKeyCode;

    default:
        return virtKeyCode;
    }
}

static bool ImplEnableMappings() {
    return G.Always || IsWindowInOurProcess(GetForegroundWindow());
}

static int ImplKeyboardDelayTime() {
    int delay = 0;
    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
    return (delay + 1) * 250;
}

static int ImplKeyboardRepeatTime() {
    int speed = 0;
    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
    return 400 - speed * 12;
}

static void CALLBACK ImplTurboTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        input->TurboValue = !input->TurboValue;
        ChangedMask changes;
        InputValue value(input->TurboValue, input->Strength, time);
        ImplProcessInputChange(*input, value, &changes);
    }
}

static void CALLBACK ImplReleaseTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        ChangedMask changes;
        InputValue value(false, input->Strength, time);
        ImplProcessInputChange(*input, value, &changes);
        input->EndTimer();
    }
}

static void CALLBACK ImplRepeatTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        ChangedMask changes;
        InputValue value(true, input->Strength, time);
        ImplProcessInputChange(*input, value, &changes);
    }
}

static void CALLBACK ImplRepeatFirstTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        ChangedMask changes;
        InputValue value(true, input->Strength, time);
        ImplProcessInputChange(*input, value, &changes);

        input->StartTimer(ImplKeyboardRepeatTime(), ImplRepeatTimerProc);
    }
}

static bool ImplCheckCanProcess(ImplInput *input) {
    if (!input->IsValid()) {
        return false;
    }

    if (G.Disable && input->DestKey != MY_VK_TOGGLE_DISABLE) {
        return false;
    }

    return true;
}

static bool ImplCanProcess(ImplInput *input, bool down, bool oldDown) {
    if (!ImplCheckCanProcess(input)) {
        return false;
    }

    if (input->Conds) {
        if (down && !oldDown) {
            ImplInputCond *cond = input->Conds;
            do {
                ImplInput *input = ImplGetInput(cond->Key);
                if (!input) {
                    return false;
                }

                if (cond->Toggle) {
                    if (input->AsyncToggle != cond->State) {
                        return false;
                    }
                } else {
                    if (input->AsyncDown != cond->State) {
                        return false;
                    }
                }

                cond = cond->Next;
            } while (cond);

            input->PassedCond = true;
        } else {
            if (!input->PassedCond) {
                return false;
            }

            if (!down) {
                input->PassedCond = false;
            }
        }
    }
    return true;
}

static bool ImplPreProcess(ImplInput *input, InputValue &v, bool &oldDown) {
    if (input->DestType.Relative && !v.Down) {
        return false;
    }

    if (input->Toggle) {
        bool toggle = v.Down && !oldDown;
        oldDown = input->ToggleValue;

        if (toggle) {
            input->ToggleValue = !input->ToggleValue;
        }

        v.Down = input->ToggleValue;
    }

    if ((input->SrcType.Repeatable && !input->DestType.Repeatable) || // e.g. keyboard -> mouse button
        input->Toggle || input->Turbo) {
        if (v.Down == oldDown) {
            return false;
        }
    }

    if (input->Turbo && !v.Down && !input->TurboValue) {
        return false;
    }

    v.Strength *= input->Strength;
    return true;
}

static void ImplPostProcess(ImplInput *input, InputValue &v, bool oldDown) {
    if (input->Turbo) {
        if (v.Down != oldDown) {
            input->TurboValue = v.Down;
            if (v.Down) {
                input->StartTimer((int)(input->Rate * 1000), ImplTurboTimerProc);
            } else {
                input->EndTimer();
            }
        }
    } else if (input->SrcType.Relative && !input->DestType.Relative) // e.g. mouse motion -> keyboard
    {
        // TODO: other modes
        if (v.Down) {
            input->StartTimer((int)(input->Rate * 1000), ImplReleaseTimerProc);
        } else {
            input->EndTimer();
        }
    } else if (!input->SrcType.Relative && input->DestType.Relative) // e.g. keyboard -> mouse motion
    {
        if (v.Down != oldDown) {
            // Repeat is unreliable for this
            if (v.Down) {
                input->StartTimer((int)(input->Rate * 1000), ImplRepeatTimerProc);
            } else {
                input->EndTimer();
            }
        }
    } else if ((!input->SrcType.Repeatable && input->DestType.Repeatable) || // e.g. mouse button -> keyboard
               (input->Toggle && input->DestType.Repeatable)) {
        if (v.Down != oldDown) {
            if (v.Down) {
                input->StartTimer(ImplKeyboardDelayTime(), ImplRepeatFirstTimerProc);
            } else {
                input->EndTimer();
            }
        }
    }
}

static bool ImplProcessInput(ImplInput *input, InputValue &v, ChangedMask *changes) {
    bool processed = false;
    for (; input; input = input->Next) {
        bool oldDown = input->AsyncDown;
        if (v.Down && !input->AsyncDown) {
            input->AsyncToggle = !input->AsyncToggle;
        }
        input->AsyncDown = v.Down;

        if (ImplCanProcess(input, v.Down, oldDown)) {
            InputValue inputV = v;
            if (ImplPreProcess(input, inputV, oldDown)) {
                ImplProcessInputChange(*input, inputV, changes);
            }
            ImplPostProcess(input, inputV, oldDown);

            if (!input->Forward && !G.Forward) {
                processed = true;
            }
        }
    }
    return processed;
}

static bool ImplGenericButtonHook(int virtKeyCode, bool down, bool extended, bool repeatable, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    virtKeyCode = ImplUnextend(virtKeyCode, extended);

    ImplInput *input = G.Keyboard.Get(virtKeyCode);
    InputValue value(down, down ? 1.0 : 0.0, time);
    return ImplProcessInput(input, value, changes);
}

static bool ImplKeyboardHook(int virtKeyCode, bool down, bool extended, int time) {
    ChangedMask changes;
    return ImplGenericButtonHook(virtKeyCode, down, extended, true, time, &changes);
}

static bool ImplProcessMouseButton(int virtKeyCode, bool down, int time, ChangedMask *changes) {
    return ImplGenericButtonHook(virtKeyCode, down, false, false, time, changes);
}

static bool ImplProcessMouseAxis(ImplMouseAxis &axis, int delta, int time, ChangedMask *changes, double scale) {
    double strength = (double)abs(delta) * scale;
    InputValue value(true, strength, time);
    InputValue cancelValue(false, strength, time);

    bool changed = false;
    changed |= ImplProcessInput(&axis.Forward, delta > 0 ? value : cancelValue, changes);
    changed |= ImplProcessInput(&axis.Backward, delta < 0 ? value : cancelValue, changes);
    return changed;
}

static bool ImplMouseMotionHook(int dx, int dy, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = G.Mouse.Motion;

    bool processed = false;
    processed |= ImplProcessMouseAxis(input.Horz, dx, time, changes, 1.0);
    processed |= ImplProcessMouseAxis(input.Vert, -dy, time, changes, 1.0);
    return processed;
}

static bool ImplMouseWheelHook(bool horiz, int delta, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = horiz ? G.Mouse.HWheel : G.Mouse.Wheel;

    constexpr double scale = (double)1 / WHEEL_DELTA;
    return ImplProcessMouseAxis(input, delta, time, changes, scale);
}

static bool ImplCheckInput(ImplInput *input) {
    for (; input; input = input->Next) {
        if (ImplCheckCanProcess(input) &&
            !input->Forward && !G.Forward) {
            return true;
        }
    }

    return false;
}

static bool ImplCheckKeyboard(int virtKeyCode, bool extended) {
    DBG_ASSERT_DLL_THREAD();

    virtKeyCode = ImplUnextend(virtKeyCode, extended);
    ImplInput *input = G.Keyboard.Get(virtKeyCode);
    return ImplCheckInput(input);
}

static bool ImplCheckMouseMotion() {
    DBG_ASSERT_DLL_THREAD();

    auto &input = G.Mouse.Motion;

    bool processed = false;
    for (auto &input : {&input.Horz.Backward, &input.Horz.Forward, &input.Vert.Backward, &input.Vert.Forward}) {
        processed |= ImplCheckInput(input);
    }
    return processed;
}

static bool ImplCheckMouseWheel(bool horiz) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = horiz ? G.Mouse.HWheel : G.Mouse.Wheel;

    bool processed = false;
    for (auto &input : {&input.Backward, &input.Forward}) {
        processed |= ImplCheckInput(input);
    }
    return processed;
}
