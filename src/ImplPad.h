#pragma once
#include "State.h"

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

    auto [sx, cx] = SinCos(motion.RX.NewValue);
    auto [sy, cy] = SinCos(motion.RY.NewValue);
    auto [sz, cz] = SinCos(motion.RZ.NewValue);
    auto sxy = sx * sy;
    motion.X.FinalAccel -= sxy * cz - cy * sz;
    motion.Y.FinalAccel -= sxy * sz + cy * cz;
    motion.Z.FinalAccel -= sy * cx;

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
