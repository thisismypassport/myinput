#pragma once
#include "State.h"
#include "Config.h"
#include "Log.h"
#include "Header.h"
#include "UtilsBuffer.h"
#include <Xinput.h> // for deadzones values (questionable)

class ChangedMask {
    UINT TouchedUsers = 0;
    UINT ChangedUsers = 0;

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

    ~ChangedMask() {
        while (TouchedUsers) {
            auto &user = G.Users[ImplNextUser(&TouchedUsers)];
            user.State.Mutex.unlock();
        }

        while (ChangedUsers) {
            auto &user = G.Users[ImplNextUser(&ChangedUsers)];
            user.SendEvent();
        }
    }
};

static bool ImplAddInputChange(ImplInput &value, bool down, int time, ChangedMask *changes, bool fromTimer = false);
static int ImplReextend(int virtKeyCode, bool *outExtended);

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

static void ImplUpdatePressedInput(ImplInput &input, int key, int time, ImplRawState &state, ChangedMask *changes) {
    if (state.Pressed) {
        ImplInput newInput(input);
        newInput.Key = key;
        newInput.Strength = state.Strength;
        ImplAddInputChange(newInput, state.Pressed, time, changes);
    }
}

static void ImplHandleTriggerModifierChange(ImplRawState &state, bool down, double strength, int time,
                                            ImplInput &input, int key, ChangedMask *changes) {
    bool oldDown = state.ModifierPressed;
    state.ModifierPressed = down;

    if (oldDown != down) {
        state.Modifier = down ? strength : 1;

        ImplUpdatePressedInput(input, key, time, state, changes);
    }
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

static void ImplHandleAxisModifierChange(ImplAxisState &axisState, bool down, double strength, int time,
                                         ImplInput &input, int posPadKey, int negPadKey, ChangedMask *changes) {
    bool oldDown = axisState.Pos.ModifierPressed || axisState.Neg.ModifierPressed;
    axisState.Pos.ModifierPressed = axisState.Neg.ModifierPressed = down;

    if (oldDown != down) {
        axisState.Pos.Modifier = axisState.Neg.Modifier = down ? strength : 1;

        ImplUpdatePressedInput(input, posPadKey, time, axisState.Pos, changes);
        ImplUpdatePressedInput(input, negPadKey, time, axisState.Neg, changes);
    }
}

static void CALLBACK ImplRotatorTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        ChangedMask changes;
        ImplAddInputChange(*input, true, time, &changes, true);
    }
}

static void ImplHandleAxisRotatorChange(ImplRawState &state, ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                        bool fromTimer, bool down, double delta, bool y1, bool y2, bool y3, bool y4, int time,
                                        ImplInput &input, int otherPosPadKey, int otherNegPadKey, ChangedMask *changes) {
    if (down && (fromTimer || !input.HasTimer()) && (otherAxisState.Pos.Pressed || otherAxisState.Neg.Pressed)) {
        if (!input.HasTimer()) {
            input.AddTimer(30, ImplRotatorTimerProc);
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

        ImplUpdatePressedInput(input, otherPosPadKey, time, otherAxisState.Pos, changes);
        ImplUpdatePressedInput(input, otherNegPadKey, time, otherAxisState.Neg, changes);
    } else if (!down && input.HasTimer()) {
        input.RemoveTimer();
    }
}

static BufferList *ImplInputBuffers() {
    static BufferList *inputBuffers = GBufferLists.Get(sizeof(INPUT));
    return inputBuffers;
}

static void ImplSendInputDelayed(void *param) {
    // Delay SendInput calls to avoid recursive/early hook calls

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

static void ImplGenerateMouseButton(int flag, int xflag, int time) {
    INPUT *input = (INPUT *)ImplInputBuffers()->Take();
    input->type = INPUT_MOUSE;
    input->mi = {};
    input->mi.dwFlags = flag;
    input->mi.mouseData = xflag;
    input->mi.time = time;
    input->mi.dwExtraInfo = ExtraInfoOurInject;

    PostAppCallback(ImplSendInputDelayed, input);
}

static bool ImplAddInputChange(ImplInput &value, bool down, int time, ChangedMask *changes, bool fromTimer) {
    if (!value.IsValid()) {
        return false;
    }

    int keyType = value.KeyType & ~MY_TYPE_FLAGS;
    if (keyType == MY_TYPE_PAD) {
        int userIndex = value.UserIndex;
        if (userIndex < 0) {
            userIndex = G.ActiveUser;
        }

        ImplUser *user = ImplGetUser(userIndex);
        if (!user) {
            return false;
        }

        ImplState &state = user->State;
        changes->TouchUser(userIndex, state);

        bool changed = false;
        switch (value.Key) {
        case MY_VK_PAD_A:
            changed = ImplHandleButtonInputChange(state.A, down);
            break;
        case MY_VK_PAD_B:
            changed = ImplHandleButtonInputChange(state.B, down);
            break;
        case MY_VK_PAD_X:
            changed = ImplHandleButtonInputChange(state.X, down);
            break;
        case MY_VK_PAD_Y:
            changed = ImplHandleButtonInputChange(state.Y, down);
            break;
        case MY_VK_PAD_START:
            changed = ImplHandleButtonInputChange(state.Start, down);
            break;
        case MY_VK_PAD_BACK:
            changed = ImplHandleButtonInputChange(state.Back, down);
            break;
        case MY_VK_PAD_DPAD_LEFT:
            changed = ImplHandleButtonInputChange(state.DL, down, &state.DR);
            break;
        case MY_VK_PAD_DPAD_RIGHT:
            changed = ImplHandleButtonInputChange(state.DR, down, &state.DL);
            break;
        case MY_VK_PAD_DPAD_UP:
            changed = ImplHandleButtonInputChange(state.DU, down, &state.DD);
            break;
        case MY_VK_PAD_DPAD_DOWN:
            changed = ImplHandleButtonInputChange(state.DD, down, &state.DU);
            break;
        case MY_VK_PAD_LSHOULDER:
            changed = ImplHandleButtonInputChange(state.LB, down);
            break;
        case MY_VK_PAD_RSHOULDER:
            changed = ImplHandleButtonInputChange(state.RB, down);
            break;
        case MY_VK_PAD_LTHUMB_PRESS:
            changed = ImplHandleButtonInputChange(state.L, down);
            break;
        case MY_VK_PAD_RTHUMB_PRESS:
            changed = ImplHandleButtonInputChange(state.R, down);
            break;
        case MY_VK_PAD_GUIDE:
            changed = ImplHandleButtonInputChange(state.Guide, down);
            break;
        case MY_VK_PAD_EXTRA:
            changed = ImplHandleButtonInputChange(state.Extra, down);
            break;

        case MY_VK_PAD_LTRIGGER:
            changed = ImplHandleTriggerInputChange(state.LT, down, value.Strength);
            break;
        case MY_VK_PAD_RTRIGGER:
            changed = ImplHandleTriggerInputChange(state.RT, down, value.Strength);
            break;

        case MY_VK_PAD_LTHUMB_UP:
            changed = ImplHandleAxisInputChange(state.LA.U, state.LA.D, state.LA.Y, state.LA.X, state.LA,
                                                1.0, false, down, value.Strength);
            break;
        case MY_VK_PAD_LTHUMB_DOWN:
            changed = ImplHandleAxisInputChange(state.LA.D, state.LA.U, state.LA.Y, state.LA.X, state.LA,
                                                -1.0, false, down, value.Strength);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT:
            changed = ImplHandleAxisInputChange(state.LA.R, state.LA.L, state.LA.X, state.LA.Y, state.LA,
                                                1.0, false, down, value.Strength);
            break;
        case MY_VK_PAD_LTHUMB_LEFT:
            changed = ImplHandleAxisInputChange(state.LA.L, state.LA.R, state.LA.X, state.LA.Y, state.LA,
                                                -1.0, false, down, value.Strength);
            break;
        case MY_VK_PAD_RTHUMB_UP:
            changed = ImplHandleAxisInputChange(state.RA.U, state.RA.D, state.RA.Y, state.RA.X, state.RA,
                                                1.0, true, down, value.Strength);
            break;
        case MY_VK_PAD_RTHUMB_DOWN:
            changed = ImplHandleAxisInputChange(state.RA.D, state.RA.U, state.RA.Y, state.RA.X, state.RA,
                                                -1.0, true, down, value.Strength);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT:
            changed = ImplHandleAxisInputChange(state.RA.R, state.RA.L, state.RA.X, state.RA.Y, state.RA,
                                                1.0, true, down, value.Strength);
            break;
        case MY_VK_PAD_RTHUMB_LEFT:
            changed = ImplHandleAxisInputChange(state.RA.L, state.RA.R, state.RA.X, state.RA.Y, state.RA,
                                                -1.0, true, down, value.Strength);
            break;

        case MY_VK_PAD_LTHUMB_HORZ_MODIFIER:
        case MY_VK_PAD_LTHUMB_VERT_MODIFIER:
        case MY_VK_PAD_LTHUMB_MODIFIER:
            if (value.Key != MY_VK_PAD_LTHUMB_VERT_MODIFIER) {
                ImplHandleAxisModifierChange(state.LA.X, down, value.Strength, time, value, MY_VK_PAD_LTHUMB_RIGHT, MY_VK_PAD_LTHUMB_LEFT, changes);
            }
            if (value.Key != MY_VK_PAD_LTHUMB_HORZ_MODIFIER) {
                ImplHandleAxisModifierChange(state.LA.Y, down, value.Strength, time, value, MY_VK_PAD_LTHUMB_UP, MY_VK_PAD_LTHUMB_DOWN, changes);
            }
            break;

        case MY_VK_PAD_RTHUMB_HORZ_MODIFIER:
        case MY_VK_PAD_RTHUMB_VERT_MODIFIER:
        case MY_VK_PAD_RTHUMB_MODIFIER:
            if (value.Key != MY_VK_PAD_RTHUMB_VERT_MODIFIER) {
                ImplHandleAxisModifierChange(state.RA.X, down, value.Strength, time, value, MY_VK_PAD_RTHUMB_RIGHT, MY_VK_PAD_RTHUMB_LEFT, changes);
            }
            if (value.Key != MY_VK_PAD_RTHUMB_HORZ_MODIFIER) {
                ImplHandleAxisModifierChange(state.RA.Y, down, value.Strength, time, value, MY_VK_PAD_RTHUMB_UP, MY_VK_PAD_RTHUMB_DOWN, changes);
            }
            break;

        case MY_VK_PAD_TRIGGER_MODIFIER:
        case MY_VK_PAD_LTRIGGER_MODIFIER:
        case MY_VK_PAD_RTRIGGER_MODIFIER:
            if (value.Key != MY_VK_PAD_RTRIGGER_MODIFIER) {
                ImplHandleTriggerModifierChange(state.LT, down, value.Strength, time, value, MY_VK_PAD_LTRIGGER, changes);
            }
            if (value.Key != MY_VK_PAD_LTRIGGER_MODIFIER) {
                ImplHandleTriggerModifierChange(state.RT, down, value.Strength, time, value, MY_VK_PAD_RTRIGGER, changes);
            }
            break;

        case MY_VK_PAD_LTHUMB_UP_ROTATOR:
            ImplHandleAxisRotatorChange(state.LA.U, state.LA.Y, state.LA.X, state.LA,
                                        fromTimer, down, value.Strength, false, false, true, true, time,
                                        value, MY_VK_PAD_LTHUMB_RIGHT, MY_VK_PAD_LTHUMB_LEFT, changes);
            break;
        case MY_VK_PAD_LTHUMB_DOWN_ROTATOR:
            ImplHandleAxisRotatorChange(state.LA.D, state.LA.Y, state.LA.X, state.LA,
                                        fromTimer, down, value.Strength, true, true, false, false, time,
                                        value, MY_VK_PAD_LTHUMB_RIGHT, MY_VK_PAD_LTHUMB_LEFT, changes);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT_ROTATOR:
            ImplHandleAxisRotatorChange(state.LA.R, state.LA.X, state.LA.Y, state.LA,
                                        fromTimer, down, value.Strength, true, false, false, true, time,
                                        value, MY_VK_PAD_LTHUMB_UP, MY_VK_PAD_LTHUMB_DOWN, changes);
            break;
        case MY_VK_PAD_LTHUMB_LEFT_ROTATOR:
            ImplHandleAxisRotatorChange(state.LA.L, state.LA.X, state.LA.Y, state.LA,
                                        fromTimer, down, value.Strength, false, true, true, false, time,
                                        value, MY_VK_PAD_LTHUMB_UP, MY_VK_PAD_LTHUMB_DOWN, changes);
            break;

        case MY_VK_PAD_RTHUMB_UP_ROTATOR:
            ImplHandleAxisRotatorChange(state.RA.U, state.RA.Y, state.RA.X, state.RA,
                                        fromTimer, down, value.Strength, false, false, true, true, time,
                                        value, MY_VK_PAD_RTHUMB_RIGHT, MY_VK_PAD_RTHUMB_LEFT, changes);
            break;
        case MY_VK_PAD_RTHUMB_DOWN_ROTATOR:
            ImplHandleAxisRotatorChange(state.RA.D, state.RA.Y, state.RA.X, state.RA,
                                        fromTimer, down, value.Strength, true, true, false, false, time,
                                        value, MY_VK_PAD_RTHUMB_RIGHT, MY_VK_PAD_RTHUMB_LEFT, changes);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT_ROTATOR:
            ImplHandleAxisRotatorChange(state.RA.R, state.RA.X, state.RA.Y, state.RA,
                                        fromTimer, down, value.Strength, true, false, false, true, time,
                                        value, MY_VK_PAD_RTHUMB_UP, MY_VK_PAD_RTHUMB_DOWN, changes);
            break;
        case MY_VK_PAD_RTHUMB_LEFT_ROTATOR:
            ImplHandleAxisRotatorChange(state.RA.L, state.RA.X, state.RA.Y, state.RA,
                                        fromTimer, down, value.Strength, false, true, true, false, time,
                                        value, MY_VK_PAD_RTHUMB_UP, MY_VK_PAD_RTHUMB_DOWN, changes);
            break;

        case MY_VK_SET_ACTIVE_USER:
            if (!down) {
                if (G.DefaultActiveUser == G.ActiveUser) {
                    G.ActiveUser = userIndex;
                }
                G.DefaultActiveUser = userIndex;
            }
            break;

        case MY_VK_HOLD_ACTIVE_USER:
            if (down) {
                G.ActiveUser = userIndex;
            } else {
                G.ActiveUser = G.DefaultActiveUser;
            }
            break;

        default:
            Fatal("Invalid pad event?!");
        }

        if (changed) {
            changes->ChangeUser(userIndex, state);
        }
    } else {
        switch (value.Key) {
        case MY_VK_RELOAD:
            if (!down) {
                ConfigReload();
            }
            break;

        case MY_VK_TOGGLE_DISABLE:
            if (!down) {
                G.Disable = !G.Disable;
                UpdateAll(); // e.g. may change mouse capture
            }
            break;

        case VK_LBUTTON:
            ImplGenerateMouseButton(down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, 0, time);
            break;
        case VK_RBUTTON:
            ImplGenerateMouseButton(down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, 0, time);
            break;
        case VK_MBUTTON:
            ImplGenerateMouseButton(down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, 0, time);
            break;
        case VK_XBUTTON1:
            ImplGenerateMouseButton(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, XBUTTON1, time);
            break;
        case VK_XBUTTON2:
            ImplGenerateMouseButton(down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP, XBUTTON2, time);
            break;

        case MY_VK_WHEEL_DOWN:
            if (down) {
                ImplGenerateMouseButton(MOUSEEVENTF_WHEEL, (int)-value.Strength, time);
            }
            break;
        case MY_VK_WHEEL_UP:
            if (down) {
                ImplGenerateMouseButton(MOUSEEVENTF_WHEEL, (int)value.Strength, time);
            }
            break;
        case MY_VK_WHEEL_LEFT:
            if (down) {
                ImplGenerateMouseButton(MOUSEEVENTF_HWHEEL, (int)-value.Strength, time);
            }
            break;
        case MY_VK_WHEEL_RIGHT:
            if (down) {
                ImplGenerateMouseButton(MOUSEEVENTF_HWHEEL, (int)value.Strength, time);
            }
            break;

        case MY_VK_MOUSE_DOWN:
        case MY_VK_MOUSE_UP:
        case MY_VK_MOUSE_LEFT:
        case MY_VK_MOUSE_RIGHT:
            // TODO: this is a bad idea to split it up like this...
            break;

        default:
            if (value.Key > 0 && value.Key < MY_VK_LAST_REAL) {
                ImplGenerateKey(value.Key, down, time);
            } else {
                Fatal("Invalid event?!");
            }
            break;
        }
    }

    return true;
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

static void CALLBACK ImplTurboTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto input = ImplInput::FromTimer(id);
    if (input) {
        input->TurboValue = !input->TurboValue;
        ChangedMask changes;
        ImplAddInputChange(*input, input->TurboValue, time, &changes, true);
    }
}

static bool ImplCheckCanProcess(ImplInput *input) {
    if (G.Disable && input->Key != MY_VK_TOGGLE_DISABLE) {
        return false;
    }

    return true;
}

static bool ImplCanProcess(ImplInput *input, bool down) {
    if (!ImplCheckCanProcess(input)) {
        return false;
    }

    if (input->Conds) {
        if (down) {
            ImplInputCond *cond = input->Conds;
            do {
                ImplInput *input = ImplGetInput(cond->Key);
                if (!input) {
                    return false;
                }

                if (input->AsyncDown != cond->State) {
                    return false;
                }

                cond = cond->Next;
            } while (cond);

            input->PassedCond = true;
        } else {
            bool passedCond = input->PassedCond;
            input->PassedCond = false;

            if (!passedCond) {
                return false;
            }
        }
    }
    return true;
}

static bool ImplProcessInput(ImplInput *input, bool down, int time, ChangedMask *changes, double *strength = nullptr) {
    bool processed = false;
    for (; input; input = input->Next) {
        bool oldDown = input->AsyncDown;
        input->AsyncDown = down;
        if (strength) {
            input->Strength = *strength; // TODO - bad? AsyncStrength vs Strength?
        }

        if (ImplCanProcess(input, down)) {
            bool inputDown = down;
            if (input->Toggle) {
                if (down && !oldDown) {
                    input->ToggleValue = !input->ToggleValue;
                    inputDown = input->ToggleValue;
                } else {
                    if (!input->Forward && !G.Forward) {
                        processed = true;
                    }
                    continue;
                }
            }

            bool done = ImplAddInputChange(*input, inputDown, time, changes);
            if (done) {
                if (!input->Forward && !G.Forward) {
                    processed = true;
                }

                if (input->Turbo) {
                    input->TurboValue = inputDown;
                    if (inputDown) {
                        input->AddTimer((int)(input->Range * 1000), ImplTurboTimerProc);
                    } else {
                        input->RemoveTimer();
                    }
                }
            }
        }
    }

    return processed;
}

static bool ImplKeyboardHook(int virtKeyCode, bool down, bool extended, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    virtKeyCode = ImplUnextend(virtKeyCode, extended);
    ImplInput *input = G.Keyboard.Get(virtKeyCode);
    return ImplProcessInput(input, down, time, changes);
}

static bool ImplKeyboardHook(int virtKeyCode, bool down, bool extended, int time) {
    ChangedMask changes;
    return ImplKeyboardHook(virtKeyCode, down, extended, time, &changes);
}

// TODO: avoid motion checking, use delta only

static bool ImplProcessMouseAxis(ImplMouseAxis &axis, int delta, int time, ChangedMask *changes) {
    double strength = abs(delta);

    bool processed = false;
    processed |= ImplProcessInput(&axis.Forward, delta > 0, time, changes, &strength);
    processed |= ImplProcessInput(&axis.Backward, delta < 0, time, changes, &strength);
    return processed;
}

static bool ImplMouseMotionHook(int dx, int dy, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = G.Mouse.Motion;

    bool processed = ImplProcessMouseAxis(input.Horz, dx, time, changes);
    processed |= ImplProcessMouseAxis(input.Vert, -dy, time, changes);
    return processed;
}

static bool ImplMouseWheelHook(bool horiz, int delta, int time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = horiz ? G.Mouse.HWheel : G.Mouse.Wheel;

    return ImplProcessMouseAxis(input, delta, time, changes);
}

static bool ImplCheckInput(ImplInput *input) {
    for (; input; input = input->Next) {
        if (input->IsValid() && ImplCheckCanProcess(input) &&
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
