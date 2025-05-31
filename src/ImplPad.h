#pragma once
#include "StateUtils.h"
#include "Header.h"
#include "ImplFeedback.h"
#define MYINPUT_HOOK_DLL_DECLSPEC __declspec(dllexport)
#include "MyInputHook.h"

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

static void ImplProcessBoolOutput(ImplBoolOutput &output, bool down, slot_t slot) {
    if (down) {
        output.Slots |= 1 << slot;
    } else {
        output.Slots &= ~(1 << slot);
    }
}

static bool ImplHandleButtonChangeGlobal(ImplButtonState &state, ImplButtonState *exclusiveState = nullptr) {
    bool down = state.Pressed.Get();
    if (exclusiveState) {
        if (exclusiveState->State && down) {
            exclusiveState->State = false;
        } else if (!down && exclusiveState->Pressed.Get()) {
            exclusiveState->State = true;
        }
    }

    bool oldDown = state.State;
    state.State = down;
    return down != oldDown;
}

static bool ImplHandleButtonChange(ImplButtonState &state, bool down, slot_t slot, ImplButtonState *exclusiveState = nullptr) {
    ImplProcessBoolOutput(state.Pressed, down, slot);
    return ImplHandleButtonChangeGlobal(state, exclusiveState);
}

static void ImplProcessStrengthOutput(ImplBoolOutput &boolOutput, ImplStrengthOutput &output, bool down, double strength, slot_t slot) {
    if (!output.InitIfNeeded(boolOutput, slot)) {
        return;
    }

    bool prevDown = (boolOutput.Slots & (1 << slot)) != 0;
    if (down && !prevDown) {
        output.Slots[slot] = strength;
        output.Combined = max(output.Combined, strength);
    } else if (!down && prevDown) {
        output.Slots[slot] = 0.0;
        output.Combined = 0.0;
        for (int i = 0; i < boolOutput.NumSlots; i++) {
            output.Combined = max(output.Combined, output.Slots[i]);
        }
    }

    ImplProcessBoolOutput(boolOutput, down, slot);
}

static bool ImplHandleTriggerChangeGlobal(ImplTriggerState &state) {
    double oldValue = state.Value;
    double value = Clamp(state.PressedStrength.Get() * state.ModifierStrength.Get(), 0.0, 1.0);
    state.Value = value;
    state.State = value > state.Threshold;

    return value != oldValue;
}

static bool ImplHandleTriggerChange(ImplTriggerState &state, bool down, double strength, slot_t slot) {
    ImplProcessStrengthOutput(state.Pressed, state.PressedStrength, down, strength, slot);
    return ImplHandleTriggerChangeGlobal(state);
}

static double ImplProcessModifierOutput(ImplBoolOutput &boolOutput, ImplModifierOutput &output, bool down, double strength, slot_t slot) {
    if (!output.InitIfNeeded(boolOutput, slot)) {
        return output.Combined;
    }

    bool prevDown = (boolOutput.Slots & (1 << slot)) != 0;
    if (down && !prevDown) {
        output.Slots[slot] = strength;
        output.Combined *= strength;
    } else if (!down && prevDown) {
        output.Slots[slot] = 1.0;
        output.Combined = 1.0;
        for (int i = 0; i < boolOutput.NumSlots; i++) {
            output.Combined *= output.Slots[i];
        }
    }

    ImplProcessBoolOutput(boolOutput, down, slot);
    return output.Combined;
}

static bool ImplHandleTriggerModifierChange(ImplTriggerState &state, bool down, double strength, slot_t slot, ChangedMask *changes) {
    double oldStrength = state.ModifierStrength.Get();
    strength = ImplProcessModifierOutput(state.Modifier, state.ModifierStrength, down, strength, slot);

    bool changed = false;
    if (oldStrength != strength && state.Pressed.Get()) {
        changed = ImplHandleTriggerChangeGlobal(state);
    }
    return changed;
}

static bool ImplHandleAxesChangeGlobal(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplUser *user) {
    double extent = Clamp(axisState.Extent, -1.0, 1.0);
    double otherExtent = Clamp(otherAxisState.Extent, -1.0, 1.0);
    constexpr double epsilon = 0.000001;

    // renormalize (in square)
    if ((axisState.Weight > 1 || otherAxisState.Weight > 1) &&
        axisState.Weight != otherAxisState.Weight &&
        (abs(extent) > epsilon || abs(otherExtent) > epsilon)) {
        double targetInfNorm = max(abs(extent), abs(otherExtent));
        extent *= axisState.Weight;
        otherExtent *= otherAxisState.Weight;
        double invInfNorm = targetInfNorm / max(abs(extent), abs(otherExtent));
        extent *= invInfNorm;
        otherExtent *= invInfNorm;
    }

    extent = Clamp(extent * axisState.RotateMultiplier * axisState.ModifierStrength.Get(), -1.0, 1.0);
    otherExtent = Clamp(otherExtent * otherAxisState.RotateMultiplier * otherAxisState.ModifierStrength.Get(), -1.0, 1.0);

    switch (user->StickShape) {
    case ImplStickShape::Circle:
        // convert square coords -> circle coords (not a great way of doing it, but others are bad too...)
        if (abs(extent) > epsilon && abs(otherExtent) > epsilon) {
            double norm = max(abs(extent), abs(otherExtent)) / sqrt(extent * extent + otherExtent * otherExtent);
            extent *= norm;
            otherExtent *= norm;
        }
        break;
    }

    double oldValue = axisState.Value;
    double oldOtherValue = otherAxisState.Value;

    axisState.Value = extent;
    otherAxisState.Value = otherExtent;

    return extent != oldValue || otherExtent != oldOtherValue;
}

static void ImplResetRotate(ImplAxisState &axisState) {
    if (axisState.RotateFakePressed) {
        axisState.RotateFakePressed = false;
        axisState.Extent = 0;
    }
    axisState.RotateMultiplier = 1.0;

    axisState.Pos.PrevPressedForRotate = axisState.Pos.Pressed.Get();
    axisState.Neg.PrevPressedForRotate = axisState.Neg.Pressed.Get();
}

static bool ImplHandleAxisChangeGlobal(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplUser *user) {
    if (axisState.Pos.Pressed.Get() != axisState.Pos.PrevPressedForRotate ||
        axisState.Neg.Pressed.Get() != axisState.Neg.PrevPressedForRotate ||
        otherAxisState.Pos.Pressed.Get() != otherAxisState.Pos.PrevPressedForRotate ||
        otherAxisState.Neg.Pressed.Get() != otherAxisState.Neg.PrevPressedForRotate) {
        ImplResetRotate(axisState);
        ImplResetRotate(otherAxisState);
    }

    tie(axisState.Extent, axisState.Weight) = axisState.GetStrengthAndWeight();

    return ImplHandleAxesChangeGlobal(axisState, otherAxisState, user);
}

static bool ImplHandleAxisChangeAdd(ImplAxisDirState &state, ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplUser *user,
                                    bool down, double strength) {
    if (down) {
        if (&state == &axisState.Neg) {
            strength = -strength;
        }

        double sum = axisState.Extent + strength;
        axisState.Extent = Clamp(sum, -1.0, 1.0);
        if (sum != axisState.Extent) {
            otherAxisState.Extent = otherAxisState.Extent / abs(sum);
        }

        ImplHandleAxesChangeGlobal(axisState, otherAxisState, user);
    } else // reset
    {
        axisState.Extent = 0;
        ImplHandleAxesChangeGlobal(axisState, otherAxisState, user);
    }
    return true;
}

static bool ImplHandleAxisChange(ImplAxisDirState &state, ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplUser *user,
                                 bool down, double strength, slot_t slot, bool isAddMapping = false) {
    if (isAddMapping) {
        return ImplHandleAxisChangeAdd(state, axisState, otherAxisState, user, down, strength);
    } else {
        ImplProcessStrengthOutput(state.Pressed, state.PressedStrength, down, strength, slot);
        return ImplHandleAxisChangeGlobal(axisState, otherAxisState, user);
    }
}

static bool ImplHandleAxisModifierChange(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplUser *user,
                                         bool down, double strength, slot_t slot, ChangedMask *changes) {
    double oldStrength = axisState.ModifierStrength.Get();
    strength = ImplProcessModifierOutput(axisState.Modifier, axisState.ModifierStrength, down, strength, slot);

    bool changed = false;
    if (oldStrength != strength) {
        changed = ImplHandleAxisChangeGlobal(axisState, otherAxisState, user);
    }
    return changed;
}

static bool ImplHandleAxisRotatorChange(ImplAxisState &axisState, ImplAxisState &otherAxisState, ImplAxesState &axesState,
                                        ImplUser *user, ImplMapping &mapping, bool down, double delta, ChangedMask *changes,
                                        bool c1, bool c2, bool c3, bool c4, bool c5, bool c6, bool c7, bool c8,
                                        double initX, double initY) {
    bool changed = false;
    if (down) {
        if (!mapping.HasTimer()) {
            mapping.StartTimerS(mapping.Rate, ImplRepeatTimerProc);
        }

        if (!otherAxisState.Pos.Pressed.Get() && !otherAxisState.Neg.Pressed.Get() && !otherAxisState.RotateFakePressed) {
            otherAxisState.RotateFakePressed = true;
            otherAxisState.RotateMultiplier = 0;
            otherAxisState.Extent = otherAxisState.ModifierStrength.Get();
        }
        if (!axisState.Pos.Pressed.Get() && !axisState.Neg.Pressed.Get() && !axisState.RotateFakePressed) {
            axisState.RotateFakePressed = true;
            axisState.RotateMultiplier = 0;
            axisState.Extent = axisState.ModifierStrength.Get();
        }
        if (!axisState.RotateMultiplier && !otherAxisState.RotateMultiplier) // when nothing was pressed
        {
            axesState.X.RotateMultiplier = initX;
            axesState.Y.RotateMultiplier = initY;
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

        auto getCornerChoice = [](bool cpre, bool cpost, double sign) {
            if (cpre == cpost) {
                return cpost ? 1 : -1;
            } else if (!cpre && cpost) {
                return sign > 0 ? 1 : -1;
            } else {
                return 0;
            }
        };

        delta *= axesState.RotateModifierStrength.Get();

        while (delta > 0) {
            double xSign = axesState.X.Extent > 0 ? 1.0 : -1.0;
            double ySign = axesState.Y.Extent > 0 ? 1.0 : -1.0;
            double xExtent = xSign * axesState.X.RotateMultiplier;
            double yExtent = ySign * axesState.Y.RotateMultiplier;

            // c1..c8 correspond to dividing the unit circle into 8 sections, going clockwise from the top.
            // their value specifies whether to rotate clockwise in this section
            if (yExtent == 1 &&
                (xExtent < 1 || (xExtent == 1 && getCornerChoice(c1, c2, xSign) < 0)) &&
                (xExtent > 0 || (xExtent == 0 && getCornerChoice(c8, c1, xSign) > 0))) {
                adjustModifierByDelta(axesState.X.RotateMultiplier, xSign * (c1 ? 1.0 : -1.0));
            } else if (xExtent == 1 &&
                       (yExtent < 1 || (yExtent == 1 && getCornerChoice(c1, c2, xSign) > 0)) &&
                       (yExtent > 0 || (yExtent == 0 && getCornerChoice(c2, c3, ySign) < 0))) {
                adjustModifierByDelta(axesState.Y.RotateMultiplier, ySign * (c2 ? -1.0 : 1.0));
            } else if (xExtent == 1 &&
                       (yExtent > -1 || (yExtent == -1 && getCornerChoice(c3, c4, -ySign) < 0)) &&
                       (yExtent < 0 || (yExtent == 0 && getCornerChoice(c2, c3, ySign) > 0))) {
                adjustModifierByDelta(axesState.Y.RotateMultiplier, ySign * (c3 ? -1.0 : 1.0));
            } else if (yExtent == -1 &&
                       (xExtent < 1 || (xExtent == 1 && getCornerChoice(c3, c4, -ySign) > 0)) &&
                       (xExtent > 0 || (xExtent == 0 && getCornerChoice(c4, c5, -xSign) < 0))) {
                adjustModifierByDelta(axesState.X.RotateMultiplier, xSign * (c4 ? -1.0 : 1.0));
            } else if (yExtent == -1 &&
                       (xExtent > -1 || (xExtent == -1 && getCornerChoice(c5, c6, ySign) < 0)) &&
                       (xExtent < 0 || (xExtent == 0 && getCornerChoice(c4, c5, -xSign) > 0))) {
                adjustModifierByDelta(axesState.X.RotateMultiplier, xSign * (c5 ? -1.0 : 1.0));
            } else if (xExtent == -1 &&
                       (yExtent > -1 || (yExtent == -1 && getCornerChoice(c5, c6, ySign) > 0)) &&
                       (yExtent < 0 || (yExtent == 0 && getCornerChoice(c6, c7, -ySign) < 0))) {
                adjustModifierByDelta(axesState.Y.RotateMultiplier, ySign * (c6 ? 1.0 : -1.0));
            } else if (xExtent == -1 &&
                       (yExtent < 1 || (yExtent == 1 && getCornerChoice(c7, c8, -xSign) < 0)) &&
                       (yExtent > 0 || (yExtent == 0 && getCornerChoice(c6, c7, -ySign) > 0))) {
                adjustModifierByDelta(axesState.Y.RotateMultiplier, ySign * (c7 ? 1.0 : -1.0));
            } else if (yExtent == 1 &&
                       (xExtent > -1 || (xExtent == -1 && getCornerChoice(c7, c8, -xSign) > 0)) &&
                       (xExtent < 0 || (xExtent == 0 && getCornerChoice(c8, c1, xSign) < 0))) {
                adjustModifierByDelta(axesState.X.RotateMultiplier, xSign * (c8 ? 1.0 : -1.0));
            } else {
                delta = 0;
            }
        }

        changed |= ImplHandleAxesChangeGlobal(axisState, otherAxisState, user);
    } else if (!down && mapping.HasTimer()) {
        mapping.EndTimer();
    }
    return changed;
}

static bool ImplHandleAxisRotatorModifierChange(ImplAxesState &axesState, ImplUser *user, bool down, double strength, slot_t slot, ChangedMask *changes) {
    double oldStrength = axesState.RotateModifierStrength.Get();
    strength = ImplProcessModifierOutput(axesState.RotateModifier, axesState.RotateModifierStrength, down, strength, slot);

    bool changed = false;
    if (oldStrength != strength) {
        changed = ImplHandleAxesChangeGlobal(axesState.X, axesState.Y, user);
    }
    return changed;
}

static bool ImplUpdateMotion(ImplMotionState &motion, DWORD time) {
    double deltaTime = (double)(time - motion.PrevTime) / 1000;

    bool changed = false;
    for (auto dim : {&motion.X, &motion.Y, &motion.Z, &motion.RX, &motion.RY, &motion.RZ}) {
        auto newSpeed = (dim->NewValue - dim->OldValue) / deltaTime;
        auto newAccel = (newSpeed - dim->Speed) / deltaTime;

        if (dim->NewValue != dim->OldValue || newSpeed != dim->Speed || newAccel != dim->Accel || newSpeed || newAccel) {
            changed = true;
        }

        dim->OldValue = dim->NewValue;
        dim->Speed = newSpeed;
        dim->Accel = newAccel;
        dim->GAccel = newAccel / ImplMotionState::GScale;
    }

    auto yaxis = motion.YAxis();
    motion.X.GAccel -= yaxis.X;
    motion.Y.GAccel -= yaxis.Y;
    motion.Z.GAccel -= yaxis.Z;

    if (changed) {
        motion.IdleTime = time;
    } else if (time - motion.IdleTime < 1000) { // just one idle event isn't enough (how many? forever?)
        changed = true;
    }

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

static void ImplHandleMotionDimChange(ImplMotionDimState &dimState, ImplUser *user, double scale, const InputValue &v, double rate, bool add = false) {
    if (add && v.Down) {
        dimState.NewValue += v.Strength * scale;
    } else {
        dimState.NewValue = v.Down ? v.Strength * scale : 0.0;
    }

    auto &motion = user->State.Motion;
    if (!motion.Timer.IsSet()) {
        motion.PrevTime = v.Time;
        motion.Timer.StartS(rate, ImplMotionTimerProc, user);
    }
}

static void ImplHandleMotionRelDimChange(ImplMotionState &state, ImplUser *user, double scale, Vector3 axis, const InputValue &v, double rate, bool add = false) {
    ImplHandleMotionDimChange(state.X, user, scale * axis.X, v, rate, add);
    ImplHandleMotionDimChange(state.Y, user, scale * axis.Y, v, rate, add);
    ImplHandleMotionDimChange(state.Z, user, scale * axis.Z, v, rate, add);
}

static bool ImplPadGetInState(ImplUser *user, int type, void *state, int size) {
    auto &in = user->State;
    switch (type) {
    case MyInputHook_InState_Basic_Type: {
        auto out = (MyInputHook_InState_Basic *)state;
        if (size < sizeof(*out)) {
            return false;
        }

        out->A = in.A.State;
        out->B = in.B.State;
        out->X = in.X.State;
        out->Y = in.Y.State;
        out->DL = in.DL.State;
        out->DR = in.DR.State;
        out->DU = in.DU.State;
        out->DD = in.DD.State;
        out->LB = in.LB.State;
        out->RB = in.RB.State;
        out->L = in.L.State;
        out->R = in.R.State;
        out->Start = in.Start.State;
        out->Back = in.Back.State;
        out->Guide = in.Guide.State;
        out->Extra = in.Extra.State;
        out->LT = in.LT.State;
        out->RT = in.RT.State;
        out->LTStr = in.LT.Value;
        out->RTStr = in.RT.Value;
        out->LY = in.LA.Y.Value;
        out->LX = in.LA.X.Value;
        out->RY = in.RA.Y.Value;
        out->RX = in.RA.X.Value;

        memset(out->Reserved, 0, sizeof(out->Reserved));
        memset(out->Reserved2, 0, sizeof(out->Reserved2));

        if (size > sizeof(*out)) {
            memset(out + 1, 0, size - sizeof(*out));
        }
    }
        return true;

    case MyInputHook_InState_Motion_Type: {
        auto out = (MyInputHook_InState_Motion *)state;
        if (size < sizeof(*out)) {
            return false;
        }

        auto copy = [&](MyInputHook_InState_Motion::Axis *out, ImplMotionDimState *in) {
            out->Pos = in->NewValue;
            out->Speed = in->Speed;
            out->Accel = in->Accel;
        };

        copy(&out->X, &in.Motion.X);
        copy(&out->Y, &in.Motion.Y);
        copy(&out->Z, &in.Motion.Z);
        copy(&out->RX, &in.Motion.RX);
        copy(&out->RY, &in.Motion.RY);
        copy(&out->RZ, &in.Motion.RZ);

        if (size > sizeof(*out)) {
            memset(out + 1, 0, size - sizeof(*out));
        }
    }
        return true;

    default:
        return false;
    }
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

static bool ImplPadSetOutState(ImplUser *user, int type, const void *state, int size) {
    switch (type) {
    case MyInputHook_OutState_Rumble_Type: {
        auto out = (const MyInputHook_OutState_Rumble *)state;
        ImplSetRumble(user, out->Low, out->High);
    }
        return true;

    default:
        return false;
    }
}
