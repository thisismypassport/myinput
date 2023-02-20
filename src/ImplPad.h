#pragma once
#include "State.h"
#include "Header.h"
#include "ImplFeedback.h"

static void CALLBACK ImplRepeatTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time);
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

    void ChangeUser(int index, ImplState &state, DWORD time) {
        int mask = (1 << index);
        if (!(ChangedUsers & mask)) {
            state.Time = time;
            state.Version++;
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
            user.Callbacks.Call(&user);
        }

        if (ChangedMouseMotion) {
            ImplGenerateMouseMotionFinish();
        }
    }
};

static void ImplHandleInputChange(ImplRawState &state, bool down, double strength) {
    state.Pressed = down;

    if (down) {
        state.Strength = strength;
    } else {
        state.Strength = 0;
    }
}

static bool ImplHandleButtonChange(ImplButtonState &state, bool down, ImplButtonState *exclusiveState = nullptr) {
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

static bool ImplHandleTriggerChange(ImplTriggerState &state, bool down, double strength) {
    ImplHandleInputChange(state, down, strength);

    double oldValue = state.Value;
    double value = Clamp(down ? state.Strength * state.Modifier : 0, 0.0, 1.0);
    state.Value = value;
    state.State = value > state.Threshold;

    return value != oldValue;
}

static bool ImplHandleTriggerModifierChange(ImplTriggerState &state, bool down, double strength, ChangedMask *changes) {
    bool changed = false;
    bool oldDown = state.ModifierPressed;
    state.ModifierPressed = down;

    if (oldDown != down) {
        state.Modifier = down ? strength : 1.0;

        if (state.Pressed) {
            changed = ImplHandleTriggerChange(state, state.Pressed, state.Strength);
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

static bool ImplHandleAxisChange(ImplRawState &state, ImplRawState &otherState, ImplAxisState &axisState, ImplAxisState &otherAxisState,
                                 ImplAxesState &axesState, double sign, bool isRight, bool down, double strength, bool add = false) {
    ImplHandleInputChange(state, down, strength);

    if (axisState.Pos.Pressed != axisState.Pos.PrevPressedForRotate || axisState.Neg.Pressed != axisState.Neg.PrevPressedForRotate ||
        otherAxisState.Pos.Pressed != otherAxisState.Pos.PrevPressedForRotate || otherAxisState.Neg.Pressed != otherAxisState.Neg.PrevPressedForRotate) {
        ImplResetRotate(axisState);
        ImplResetRotate(otherAxisState);
    }

    double extent = down ? state.Strength * state.Modifier * sign : otherState.Pressed ? otherState.Strength * otherState.Modifier * -sign
                                                                                       : 0;
    extent = Clamp(extent, -1.0, 1.0);

    if (add && down) {
        double sum = extent + axisState.Extent;
        extent = Clamp(sum, -1.0, 1.0);
        if (sum != extent) {
            otherAxisState.Extent /= abs(sum);
        }
    }

    axisState.Extent = extent;

    double value = axisState.Extent * axisState.RotateModifier;
    double otherValue = otherAxisState.Extent * otherAxisState.RotateModifier;

    double epsilon = 0.000001;
    if (abs(value) > epsilon && abs(otherValue) > epsilon) {
        double norm = max(abs(value), abs(otherValue)) / sqrt(value * value + otherValue * otherValue);
        value *= norm;
        otherValue *= norm;
    }

    double oldValue = axisState.Value;
    double oldOtherValue = otherAxisState.Value;

    axisState.Value = value;
    otherAxisState.Value = otherValue;

    return value != oldValue || otherValue != oldOtherValue;
}

static bool ImplUpdateAxisOnModifierChange(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                           bool isRight) {
    bool changed = false;
    if (axisState.Pos.Pressed) {
        changed |= ImplHandleAxisChange(axisState.Pos, axisState.Neg, axisState, otherAxisState, axesState,
                                        1.0, isRight, axisState.Pos.Pressed, axisState.Pos.Strength);
    }
    if (axisState.Neg.Pressed) {
        changed |= ImplHandleAxisChange(axisState.Neg, axisState.Pos, axisState, otherAxisState, axesState,
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
                                        ImplMapping &mapping, bool down, double delta, ChangedMask *changes) {
    bool changed = false;
    if (down && (otherAxisState.Pos.Pressed || otherAxisState.Neg.Pressed)) {
        if (!mapping.HasTimer()) {
            mapping.StartTimerMs(30, ImplRepeatTimerProc);
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
    } else if (!down && mapping.HasTimer()) {
        mapping.EndTimer();
    }
    return changed;
}

static bool ImplUpdateMotion(ImplMotionState &motion, DWORD time) {
    double deltaTime = (double)(time - motion.PrevTime) / 1000;

    bool changed = false;
    for (auto dim : {&motion.X, &motion.Y, &motion.Z, &motion.RX, &motion.RY, &motion.RZ}) {
        auto newSpeed = (dim->NewValue - dim->OldValue) / deltaTime;
        auto newAccel = (newSpeed - dim->Speed) / deltaTime;

        if (dim->NewValue != dim->OldValue || newSpeed != dim->Speed || newAccel != dim->Accel) {
            changed = true;
        }

        dim->OldValue = dim->NewValue;
        dim->Speed = newSpeed;
        dim->Accel = newAccel;
        dim->FinalAccel = newAccel / ImplMotionState::GScale;
    }

    auto yaxis = motion.YAxis();
    motion.X.FinalAccel -= yaxis.X;
    motion.Y.FinalAccel -= yaxis.Y;
    motion.Z.FinalAccel -= yaxis.Z;

    motion.PrevTime = time;
    return changed;
}

static void CALLBACK ImplMotionTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto user = GTimerData.From<ImplUser>(id);
    if (user) {
        auto &motion = user->State.Motion;
        int userIdx = user->Device->UserIdx;
        if (time != motion.PrevTime) {
            ChangedMask changed;
            changed.TouchUser(userIdx, user->State);

            if (ImplUpdateMotion(motion, time)) {
                changed.ChangeUser(userIdx, user->State, time);
            } else {
                user->State.Motion.Timer.End();
            }
        }
    }
}

static void ImplHandleMotionDimChange(ImplMotionDimState &dimState, ImplUser *user, double scale, const InputValue &v, bool add = false) {
    if (add && v.Down) {
        dimState.NewValue += v.Strength * scale;
    } else {
        dimState.NewValue = v.Down ? v.Strength * scale : 0.0;
    }

    auto &motion = user->State.Motion;
    if (!motion.Timer.IsSet()) {
        motion.PrevTime = v.Time;
        motion.Timer.StartMs(10, ImplMotionTimerProc, user);
    }
}

static void ImplHandleMotionRelDimChange(ImplMotionState &state, ImplUser *user, double scale, Vector3 axis, const InputValue &v, bool add = false) {
    ImplHandleMotionDimChange(state.X, user, scale * axis.X, v, add);
    ImplHandleMotionDimChange(state.Y, user, scale * axis.Y, v, add);
    ImplHandleMotionDimChange(state.Z, user, scale * axis.Z, v, add);
}

static bool ImplPadGetState(ImplUser *user, int key, int type, void *dest) {
    auto &state = user->State;
    switch (type) {
    case 'b': {
        bool &out = *(bool *)dest;
        switch (key) {
        case MY_VK_PAD_A:
            out = state.A.State;
            break;
        case MY_VK_PAD_B:
            out = state.B.State;
            break;
        case MY_VK_PAD_X:
            out = state.X.State;
            break;
        case MY_VK_PAD_Y:
            out = state.Y.State;
            break;
        case MY_VK_PAD_START:
            out = state.Start.State;
            break;
        case MY_VK_PAD_BACK:
            out = state.Back.State;
            break;
        case MY_VK_PAD_DPAD_LEFT:
            out = state.DL.State;
            break;
        case MY_VK_PAD_DPAD_RIGHT:
            out = state.DR.State;
            break;
        case MY_VK_PAD_DPAD_UP:
            out = state.DU.State;
            break;
        case MY_VK_PAD_DPAD_DOWN:
            out = state.DD.State;
            break;
        case MY_VK_PAD_LSHOULDER:
            out = state.LB.State;
            break;
        case MY_VK_PAD_RSHOULDER:
            out = state.RB.State;
            break;
        case MY_VK_PAD_LTHUMB_PRESS:
            out = state.L.State;
            break;
        case MY_VK_PAD_RTHUMB_PRESS:
            out = state.R.State;
            break;
        case MY_VK_PAD_GUIDE:
            out = state.Guide.State;
            break;
        case MY_VK_PAD_EXTRA:
            out = state.Extra.State;
            break;
        case MY_VK_PAD_LTRIGGER:
            out = state.LT.State;
            break;
        case MY_VK_PAD_RTRIGGER:
            out = state.RT.State;
            break;
        default:
            return false;
        }
    } break;

    case 'd': {
        double &out = *(double *)dest;
        switch (key) {
        case MY_VK_PAD_LTRIGGER:
            out = state.LT.Value;
            break;
        case MY_VK_PAD_RTRIGGER:
            out = state.RT.Value;
            break;
        case MY_VK_PAD_LTHUMB_UP:
            out = state.LA.Y.Value;
            break;
        case MY_VK_PAD_LTHUMB_DOWN:
            out = -state.LA.Y.Value;
            break;
        case MY_VK_PAD_LTHUMB_LEFT:
            out = -state.LA.X.Value;
            break;
        case MY_VK_PAD_LTHUMB_RIGHT:
            out = state.LA.X.Value;
            break;
        case MY_VK_PAD_RTHUMB_UP:
            out = state.RA.Y.Value;
            break;
        case MY_VK_PAD_RTHUMB_DOWN:
            out = -state.RA.Y.Value;
            break;
        case MY_VK_PAD_RTHUMB_LEFT:
            out = -state.RA.X.Value;
            break;
        case MY_VK_PAD_RTHUMB_RIGHT:
            out = state.RA.X.Value;
            break;
        case MY_VK_PAD_MOTION_UP:
            out = state.Motion.Y.NewValue;
            break;
        case MY_VK_PAD_MOTION_DOWN:
            out = -state.Motion.Y.NewValue;
            break;
        case MY_VK_PAD_MOTION_RIGHT:
            out = state.Motion.X.NewValue;
            break;
        case MY_VK_PAD_MOTION_LEFT:
            out = -state.Motion.X.NewValue;
            break;
        case MY_VK_PAD_MOTION_NEAR:
            out = state.Motion.Z.NewValue;
            break;
        case MY_VK_PAD_MOTION_FAR:
            out = -state.Motion.Z.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_UP:
            out = -state.Motion.RX.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_DOWN:
            out = state.Motion.RX.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_RIGHT:
            out = state.Motion.RY.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_LEFT:
            out = -state.Motion.RY.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_CW:
            out = -state.Motion.RZ.NewValue;
            break;
        case MY_VK_PAD_MOTION_ROT_CCW:
            out = state.Motion.RZ.NewValue;
            break;
        default:
            return false;
        }
    } break;

    case 'V': {
        double &out = *(double *)dest;
        switch (key) {
        case MY_VK_PAD_MOTION_UP:
            out = state.Motion.Y.Speed;
            break;
        case MY_VK_PAD_MOTION_DOWN:
            out = -state.Motion.Y.Speed;
            break;
        case MY_VK_PAD_MOTION_RIGHT:
            out = state.Motion.X.Speed;
            break;
        case MY_VK_PAD_MOTION_LEFT:
            out = -state.Motion.X.Speed;
            break;
        case MY_VK_PAD_MOTION_NEAR:
            out = state.Motion.Z.Speed;
            break;
        case MY_VK_PAD_MOTION_FAR:
            out = -state.Motion.Z.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_UP:
            out = -state.Motion.RX.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_DOWN:
            out = state.Motion.RX.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_RIGHT:
            out = state.Motion.RY.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_LEFT:
            out = -state.Motion.RY.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_CW:
            out = -state.Motion.RZ.Speed;
            break;
        case MY_VK_PAD_MOTION_ROT_CCW:
            out = state.Motion.RZ.Speed;
            break;
        default:
            return false;
        }
    } break;

    case 'A': {
        double &out = *(double *)dest;
        switch (key) {
        case MY_VK_PAD_MOTION_UP:
            out = state.Motion.Y.Accel;
            break;
        case MY_VK_PAD_MOTION_DOWN:
            out = -state.Motion.Y.Accel;
            break;
        case MY_VK_PAD_MOTION_RIGHT:
            out = state.Motion.X.Accel;
            break;
        case MY_VK_PAD_MOTION_LEFT:
            out = -state.Motion.X.Accel;
            break;
        case MY_VK_PAD_MOTION_NEAR:
            out = state.Motion.Z.Accel;
            break;
        case MY_VK_PAD_MOTION_FAR:
            out = -state.Motion.Z.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_UP:
            out = -state.Motion.RX.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_DOWN:
            out = state.Motion.RX.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_RIGHT:
            out = state.Motion.RY.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_LEFT:
            out = -state.Motion.RY.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_CW:
            out = -state.Motion.RZ.Accel;
            break;
        case MY_VK_PAD_MOTION_ROT_CCW:
            out = state.Motion.RZ.Accel;
            break;
        default:
            return false;
        }
    } break;

    default:
        return false;
    }
    return true;
}

static void ImplSetRumble(ImplUser *user, double lowFreq, double highFreq) {
    if (G.Debug) {
        LOG << "Rumble " << lowFreq << ", " << highFreq << END;
    }

    user->State.Feedback.LowRumble = lowFreq;
    user->State.Feedback.HighRumble = highFreq;

    if (G.RumbleWindow) {
        ShowWindowRumble(lowFreq, highFreq);
    }
}

static bool ImplPadSetState(ImplUser *user, int key, int type, const void *src) {
    switch (type) {
    case 'd': {
        double val = *(const double *)src;
        switch (key) {
        case MY_VK_PAD_RUMBLE_LOW:
            ImplSetRumble(user, val, user->State.Feedback.HighRumble);
            break;
        case MY_VK_PAD_RUMBLE_HIGH:
            ImplSetRumble(user, user->State.Feedback.LowRumble, val);
            break;
        default:
            return false;
        }
    } break;

    default:
        return false;
    }
    return true;
}
