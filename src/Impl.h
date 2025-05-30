#pragma once
#include "StateUtils.h"
#include "Config.h"
#include "LogUtils.h"
#include "Header.h"
#include "UtilsBuffer.h"
#include "ImplPad.h"
#include "ImplKeyMouse.h"

static bool ImplProcessMapping(ImplMapping *mapping, const InputValue &v, ChangedMask *changes, bool oldDown, bool reset);
static void ImplToggleDisable();
static void ImplToggleAlways();
static void ImplToggleConnected(void *userId);

static void ImplHandleCustomChange(int index, InputValue &v, slot_t slot, ImplMapping &mapping) {
    if ((size_t)index < G.CustomKeys.size()) {
        auto custKey = G.CustomKeys[index].get();
        ImplProcessStrengthOutput(custKey->Key.Output, custKey->Strength, v.Down, v.Strength, slot);

        if (custKey->Callback) {
            InputValue newV(custKey->Key.Output.Get(), custKey->Strength.Get(), v.Time);
            custKey->Callback(newV, &mapping);
        }
    }
}

static void ImplLoadConfig(void *data) {
    auto ptr = (SharedPtr<string> *)data;
    if (*ptr) {
        ConfigLoad(PathFromStr(ptr->get()->c_str()));
    }
    delete ptr;
}

static void ImplProcess(ImplMapping &mapping, InputValue &v, ChangedMask *changes) {
    key_t key = mapping.DestKey;
    slot_t slot = mapping.DestSlot;
    if (mapping.DestType.OfUser) {
        int userIndex = mapping.DestUser;
        if (userIndex < 0) {
            userIndex = G.ActiveUser;
        }

        bool isCmdKey = key >= MY_VK_FIRST_USER_CMD && key < MY_VK_LAST_USER_CMD;
        ImplUser *user = ImplGetUser(userIndex, isCmdKey);
        if (!user) {
            return;
        }

        ImplState &state = user->State;
        changes->TouchUser(userIndex, state);

        bool changed = false;
        switch (key) {
        case MY_VK_PAD_A:
            changed = ImplHandleButtonChange(state.A, v.Down, slot);
            break;
        case MY_VK_PAD_B:
            changed = ImplHandleButtonChange(state.B, v.Down, slot);
            break;
        case MY_VK_PAD_X:
            changed = ImplHandleButtonChange(state.X, v.Down, slot);
            break;
        case MY_VK_PAD_Y:
            changed = ImplHandleButtonChange(state.Y, v.Down, slot);
            break;
        case MY_VK_PAD_START:
            changed = ImplHandleButtonChange(state.Start, v.Down, slot);
            break;
        case MY_VK_PAD_BACK:
            changed = ImplHandleButtonChange(state.Back, v.Down, slot);
            break;
        case MY_VK_PAD_DPAD_LEFT:
            changed = ImplHandleButtonChange(state.DL, v.Down, slot, &state.DR);
            break;
        case MY_VK_PAD_DPAD_RIGHT:
            changed = ImplHandleButtonChange(state.DR, v.Down, slot, &state.DL);
            break;
        case MY_VK_PAD_DPAD_UP:
            changed = ImplHandleButtonChange(state.DU, v.Down, slot, &state.DD);
            break;
        case MY_VK_PAD_DPAD_DOWN:
            changed = ImplHandleButtonChange(state.DD, v.Down, slot, &state.DU);
            break;
        case MY_VK_PAD_LSHOULDER:
            changed = ImplHandleButtonChange(state.LB, v.Down, slot);
            break;
        case MY_VK_PAD_RSHOULDER:
            changed = ImplHandleButtonChange(state.RB, v.Down, slot);
            break;
        case MY_VK_PAD_LTHUMB_PRESS:
            changed = ImplHandleButtonChange(state.L, v.Down, slot);
            break;
        case MY_VK_PAD_RTHUMB_PRESS:
            changed = ImplHandleButtonChange(state.R, v.Down, slot);
            break;
        case MY_VK_PAD_GUIDE:
            changed = ImplHandleButtonChange(state.Guide, v.Down, slot);
            break;
        case MY_VK_PAD_EXTRA:
            changed = ImplHandleButtonChange(state.Extra, v.Down, slot);
            break;

        case MY_VK_PAD_LTRIGGER:
            changed = ImplHandleTriggerChange(state.LT, v.Down, v.Strength, slot);
            break;
        case MY_VK_PAD_RTRIGGER:
            changed = ImplHandleTriggerChange(state.RT, v.Down, v.Strength, slot);
            break;

        case MY_VK_PAD_LTHUMB_UP:
            changed = ImplHandleAxisChange(state.LA.U, state.LA.Y, state.LA.X, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_LTHUMB_DOWN:
            changed = ImplHandleAxisChange(state.LA.D, state.LA.Y, state.LA.X, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT:
            changed = ImplHandleAxisChange(state.LA.R, state.LA.X, state.LA.Y, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_LTHUMB_LEFT:
            changed = ImplHandleAxisChange(state.LA.L, state.LA.X, state.LA.Y, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_RTHUMB_UP:
            changed = ImplHandleAxisChange(state.RA.U, state.RA.Y, state.RA.X, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_RTHUMB_DOWN:
            changed = ImplHandleAxisChange(state.RA.D, state.RA.Y, state.RA.X, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT:
            changed = ImplHandleAxisChange(state.RA.R, state.RA.X, state.RA.Y, user, v.Down, v.Strength, slot, mapping.Add);
            break;
        case MY_VK_PAD_RTHUMB_LEFT:
            changed = ImplHandleAxisChange(state.RA.L, state.RA.X, state.RA.Y, user, v.Down, v.Strength, slot, mapping.Add);
            break;

        case MY_VK_PAD_MOTION_UP:
            ImplHandleMotionRelDimChange(state.Motion, user, ImplMotionState::PosScale,
                                         state.Motion.YAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_DOWN:
            ImplHandleMotionRelDimChange(state.Motion, user, -ImplMotionState::PosScale,
                                         state.Motion.YAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_RIGHT:
            ImplHandleMotionRelDimChange(state.Motion, user, ImplMotionState::PosScale,
                                         state.Motion.XAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_LEFT:
            ImplHandleMotionRelDimChange(state.Motion, user, -ImplMotionState::PosScale,
                                         state.Motion.XAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_NEAR:
            ImplHandleMotionRelDimChange(state.Motion, user, ImplMotionState::PosScale,
                                         state.Motion.ZAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_FAR:
            ImplHandleMotionRelDimChange(state.Motion, user, -ImplMotionState::PosScale,
                                         state.Motion.ZAxis(), v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_UP:
            ImplHandleMotionDimChange(state.Motion.RX, user, -ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_DOWN:
            ImplHandleMotionDimChange(state.Motion.RX, user, ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_RIGHT:
            ImplHandleMotionDimChange(state.Motion.RY, user, ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_LEFT:
            ImplHandleMotionDimChange(state.Motion.RY, user, -ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_CW:
            ImplHandleMotionDimChange(state.Motion.RZ, user, -ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;
        case MY_VK_PAD_MOTION_ROT_CCW:
            ImplHandleMotionDimChange(state.Motion.RZ, user, ImplMotionState::RotScale, v, mapping.Rate, mapping.Add);
            break;

        case MY_VK_PAD_LTHUMB_HORZ_MODIFIER:
            changed = ImplHandleAxisModifierChange(state.LA.X, state.LA.Y, user, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_LTHUMB_VERT_MODIFIER:
            changed = ImplHandleAxisModifierChange(state.LA.Y, state.LA.X, user, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_RTHUMB_HORZ_MODIFIER:
            changed = ImplHandleAxisModifierChange(state.RA.X, state.RA.Y, user, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_RTHUMB_VERT_MODIFIER:
            changed = ImplHandleAxisModifierChange(state.RA.Y, state.RA.X, user, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_LTRIGGER_MODIFIER:
            changed = ImplHandleTriggerModifierChange(state.LT, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_RTRIGGER_MODIFIER:
            changed = ImplHandleTriggerModifierChange(state.RT, v.Down, v.Strength, slot, changes);
            break;

        case MY_VK_PAD_LTHUMB_UP_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.Y, state.LA.X, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, false, false, true, true, true, true, 0, 1);
            break;
        case MY_VK_PAD_LTHUMB_DOWN_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.Y, state.LA.X, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, true, true, false, false, false, false, 0, -1);
            break;
        case MY_VK_PAD_LTHUMB_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.X, state.LA.Y, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, false, false, false, false, true, true, 1, 0);
            break;
        case MY_VK_PAD_LTHUMB_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.X, state.LA.Y, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, true, true, true, true, false, false, -1, 0);
            break;
        case MY_VK_PAD_LTHUMB_UP_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.Y, state.LA.X, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, false, true, true, true, true, false, -1, 1);
            break;
        case MY_VK_PAD_LTHUMB_DOWN_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.Y, state.LA.X, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  false, true, true, true, true, false, false, false, -1, -1);
            break;
        case MY_VK_PAD_LTHUMB_UP_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.X, state.LA.Y, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  true, false, false, false, false, true, true, true, 1, 1);
            break;
        case MY_VK_PAD_LTHUMB_DOWN_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.LA.X, state.LA.Y, state.LA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, true, false, false, false, false, true, 1, -1);
            break;
        case MY_VK_PAD_RTHUMB_UP_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.Y, state.RA.X, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, false, false, true, true, true, true, 0, 1);
            break;
        case MY_VK_PAD_RTHUMB_DOWN_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.Y, state.RA.X, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, true, true, false, false, false, false, 0, -1);
            break;
        case MY_VK_PAD_RTHUMB_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.X, state.RA.Y, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, false, false, false, false, true, true, 1, 0);
            break;
        case MY_VK_PAD_RTHUMB_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.X, state.RA.Y, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, true, true, true, true, false, false, -1, 0);
            break;
        case MY_VK_PAD_RTHUMB_UP_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.Y, state.RA.X, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  false, false, false, true, true, true, true, false, -1, 1);
            break;
        case MY_VK_PAD_RTHUMB_DOWN_LEFT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.Y, state.RA.X, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  false, true, true, true, true, false, false, false, -1, -1);
            break;
        case MY_VK_PAD_RTHUMB_UP_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.X, state.RA.Y, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  true, false, false, false, false, true, true, true, 1, 1);
            break;
        case MY_VK_PAD_RTHUMB_DOWN_RIGHT_ROTATOR:
            changed = ImplHandleAxisRotatorChange(state.RA.X, state.RA.Y, state.RA, user, mapping, v.Down, v.Strength, changes,
                                                  true, true, true, false, false, false, false, true, 1, -1);
            break;

        case MY_VK_PAD_LTHUMB_ROTATOR_MODIFIER:
            changed = ImplHandleAxisRotatorModifierChange(state.LA, user, v.Down, v.Strength, slot, changes);
            break;
        case MY_VK_PAD_RTHUMB_ROTATOR_MODIFIER:
            changed = ImplHandleAxisRotatorModifierChange(state.RA, user, v.Down, v.Strength, slot, changes);
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

        case MY_VK_TOGGLE_CONNECTED:
            if (!v.Down) {
                PostAppCallback(ImplToggleConnected, (void *)(uintptr_t)userIndex);
            }
            break;

        default:
            Fatal("Invalid pad input action?!");
        }

        if (changed) {
            changes->ChangeUser(userIndex, state, v.Time);
        }
    } else {
        switch (key) {
        case MY_VK_RELOAD:
            if (!v.Down) {
                PostAppCallback(ConfigReload); // (may have ptrs up the stack)
            }
            break;

        case MY_VK_TOGGLE_DISABLE:
            if (!v.Down) {
                PostAppCallback(ImplToggleDisable);
            }
            break;

        case MY_VK_TOGGLE_ALWAYS:
            if (!v.Down) {
                PostAppCallback(ImplToggleAlways);
            }
            break;

        case MY_VK_TOGGLE_HIDE_CURSOR:
            if (!v.Down) {
                G.HideCursor = !G.HideCursor;
                UpdateHideCursor();
            }
            break;

        case MY_VK_TOGGLE_SPARE_FOR_DEBUG:
            if (!v.Down) {
                G.SpareForDebug = !G.SpareForDebug;
            }
            break;

        case MY_VK_LOAD_CONFIG:
            if (!v.Down) {
                PostAppCallback(ImplLoadConfig, new SharedPtr<string>(mapping.Data));
            }
            break;

        case MY_VK_NONE:
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
            ImplGenerateMouseMotion(0, v.Strength, v.Time, changes);
            break;
        case MY_VK_MOUSE_UP:
            ImplGenerateMouseMotion(0, -v.Strength, v.Time, changes);
            break;
        case MY_VK_MOUSE_LEFT:
            ImplGenerateMouseMotion(-v.Strength, 0, v.Time, changes);
            break;
        case MY_VK_MOUSE_RIGHT:
            ImplGenerateMouseMotion(v.Strength, 0, v.Time, changes);
            break;

        default:
            if (key > 0 && key < MY_VK_LAST_REAL) {
                ImplHandleKeyChange(key, v.Down, v.Time, slot);
            } else if (key >= MY_VK_CUSTOM_START) {
                ImplHandleCustomChange(key - MY_VK_CUSTOM_START, v, slot, mapping);
            } else {
                Fatal("Invalid input action?!");
            }
            break;
        }
    }
}

static void CALLBACK ImplTurboTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto mapping = ImplMapping::FromTimer(id);
    if (mapping) {
        mapping->TurboValue = !mapping->TurboValue;
        ChangedMask changes;
        InputValue value(mapping->TurboValue, mapping->Strength, time);
        ImplProcess(*mapping, value, &changes);
    }
}

static void CALLBACK ImplReleaseTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto mapping = ImplMapping::FromTimer(id);
    if (mapping) {
        ChangedMask changes;
        InputValue value(false, mapping->Strength, time);
        ImplProcessMapping(mapping, value, &changes, true, false);
    }
}

static void CALLBACK ImplRepeatTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto mapping = ImplMapping::FromTimer(id);
    if (mapping) {
        ChangedMask changes;
        InputValue value(true, mapping->Strength, time);
        ImplProcess(*mapping, value, &changes);
    }
}

static void CALLBACK ImplRepeatFirstTimerProc(HWND window, UINT msg, UINT_PTR id, DWORD time) {
    DBG_ASSERT_DLL_THREAD();
    auto mapping = ImplMapping::FromTimer(id);
    if (mapping) {
        ChangedMask changes;
        InputValue value(true, mapping->Strength, time);
        ImplProcess(*mapping, value, &changes);

        mapping->StartTimerMs(ImplKeyboardRepeatTime(), ImplRepeatTimerProc);
    }
}

static bool ImplCheckCondsOr(ImplCond *cond);
static bool ImplCheckCondsAnd(ImplCond *cond);

static bool ImplCheckCond(ImplCond *cond) {
    ImplInput *input = ImplGetInput(cond->Key, cond->User);
    if (!input) {
        switch (cond->Key) {
        case MY_VK_META_COND_AND:
            return ImplCheckCondsAnd(cond->Child) == cond->State;
        case MY_VK_META_COND_OR:
            return ImplCheckCondsOr(cond->Child) == cond->State;
        }
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

    return true;
}

static bool ImplCheckCondsOr(ImplCond *cond) {
    do {
        if (ImplCheckCond(cond)) {
            return true;
        }

        cond = cond->Next;
    } while (cond);

    return false;
}

static bool ImplCheckCondsAnd(ImplCond *cond) {
    do {
        if (!ImplCheckCond(cond)) {
            return false;
        }

        cond = cond->Next;
    } while (cond);

    return true;
}

static bool ImplCanProcess(ImplMapping *mapping, bool down, bool oldDown, bool check = false) {
    if (G.Disable && mapping->DestKey != MY_VK_TOGGLE_DISABLE) {
        return false;
    }

    if (mapping->Conds) {
        if (check || mapping->SrcType.Relative) {
            if (!ImplCheckCondsAnd(mapping->Conds)) {
                return false;
            }
        } else if (down && !oldDown) {
            if (!ImplCheckCondsAnd(mapping->Conds)) {
                return false;
            }

            mapping->PassedCond = true;
        } else {
            if (!mapping->PassedCond) {
                return false;
            }

            if (!down) {
                mapping->PassedCond = false;
            }
        }
    }
    return true;
}

static bool ImplPreProcess(ImplMapping *mapping, InputValue &v, bool &oldDown, bool reset) {
    /*if (mapping->Release) - could add, but would probably want to activate immediately?
    {
        oldDown = !oldDown;
        v.Down = !v.Down;
        v.Strength = v.Down ? mapping->Strength : 0;
    }*/

    if (mapping->DestType.Relative && !v.Down) {
        return false;
    }

    if (mapping->Add && !v.Down) {
        if (!mapping->Conds) {
            reset = true;
        }

        if (!reset) {
            return false;
        }
    }

    if (mapping->Toggle) {
        bool toggle = v.Down && !oldDown;
        oldDown = mapping->ToggleValue;

        if (reset) {
            mapping->ToggleValue = false;
        } else if (toggle) {
            mapping->ToggleValue = !mapping->ToggleValue;
        }

        v.Down = mapping->ToggleValue;
    }

    if ((mapping->SrcType.Repeatable && !mapping->DestType.Repeatable) || // e.g. keyboard -> mouse button
        mapping->Toggle || mapping->Turbo || reset) {
        if (v.Down == oldDown) {
            return false;
        }
    }

    if (mapping->Turbo && !v.Down && !mapping->TurboValue) {
        return false;
    }

    v.Strength *= mapping->Strength;
    return true;
}

static void ImplPostProcess(ImplMapping *mapping, InputValue &v, bool oldDown) {
    if (mapping->Turbo) {
        if (v.Down != oldDown) {
            mapping->TurboValue = v.Down;
            if (v.Down) {
                mapping->StartTimerS(mapping->Rate, ImplTurboTimerProc);
            } else {
                mapping->EndTimer();
            }
        }
    } else if (mapping->SrcType.Relative && !mapping->DestType.Relative) // e.g. mouse motion -> keyboard
    {
        if (v.Down) {
            mapping->StartTimerS(mapping->Rate, ImplReleaseTimerProc);
        } else {
            mapping->EndTimer();
        }
    } else if (!mapping->SrcType.Relative && (mapping->DestType.Relative || mapping->Add)) // e.g. keyboard -> mouse motion
    {
        if (v.Down != oldDown) {
            // Repeat is unreliable for this
            if (v.Down) {
                mapping->StartTimerS(mapping->Rate, ImplRepeatTimerProc);
            } else {
                mapping->EndTimer();
            }
        }
    } else if ((!mapping->SrcType.Repeatable && mapping->DestType.Repeatable) || // e.g. mouse button -> keyboard
               (mapping->Toggle && mapping->DestType.Repeatable)) {
        if (v.Down != oldDown) {
            if (v.Down) {
                mapping->StartTimerMs(ImplKeyboardDelayTime(), ImplRepeatFirstTimerProc);
            } else {
                mapping->EndTimer();
            }
        }
    }
}

static bool ImplProcessMapping(ImplMapping *mapping, const InputValue &v, ChangedMask *changes, bool oldDown, bool reset) {
    bool processed = false;
    if (ImplCanProcess(mapping, v.Down, oldDown) || reset) {
        InputValue mapV = v;
        if (ImplPreProcess(mapping, mapV, oldDown, reset)) {
            ImplProcess(*mapping, mapV, changes);
        }
        ImplPostProcess(mapping, mapV, oldDown);

        if (!mapping->Forward && !G.Forward) {
            processed = true;
        }
    }
    return processed;
}

static void ImplProcessReset(ImplReset *reset, DWORD time, ChangedMask *changes) {
    ImplMapping *mapping = reset->Mapping;
    if ((mapping->PassedCond || mapping->Add || mapping->SrcType.Relative) && !ImplCheckCondsAnd(mapping->Conds)) {
        InputValue value(false, mapping->Strength, time);
        ImplProcessMapping(mapping, value, changes, true, true); // passing oldDown=true here seems bad...
    }
}

static bool ImplProcessInput(ImplInput *input, const InputValue &v, ChangedMask *changes, bool relative = false, bool reset = false) {
    if (G.Paused) {
        return false;
    }

    bool oldDown = input->AsyncDown;
    if (v.Down && !input->AsyncDown) {
        input->AsyncToggle = !input->AsyncToggle;
    }
    input->AsyncDown = v.Down;

    bool processed = false;
    for (ImplMapping *mapping = input->Mappings; mapping; mapping = mapping->Next) {
        if (ImplProcessMapping(mapping, v, changes, oldDown, reset)) {
            processed = true;
        }
    }

    if (v.Down != oldDown) {
        for (ImplReset *reset = input->Resets; reset; reset = reset->Next) {
            ImplProcessReset(reset, v.Time, changes);
        }
    }

    if (!relative) {
        if (v.Down) {
            input->ObservedPress = true;
        } else if (!input->ObservedPress) {
            processed = false; // forward releases of presses started while inactive
        }
    }

    return processed;
}

static bool ImplGenericButtonHook(int virtKeyCode, bool down, bool extended, bool repeatable, DWORD time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    virtKeyCode = ImplUnextend(virtKeyCode, extended);

    ImplInput *input = G.Keyboard.Get(virtKeyCode);
    InputValue value(down, down ? 1.0 : 0.0, time);
    return input ? ImplProcessInput(input, value, changes) : false;
}

static bool ImplKeyboardHook(int virtKeyCode, bool down, bool extended, DWORD time) {
    ChangedMask changes;
    return ImplGenericButtonHook(virtKeyCode, down, extended, true, time, &changes);
}

static bool ImplProcessMouseButton(int virtKeyCode, bool down, DWORD time, ChangedMask *changes) {
    return ImplGenericButtonHook(virtKeyCode, down, false, false, time, changes);
}

static bool ImplProcessMouseAxis(ImplMouseAxis &axis, int delta, DWORD time, ChangedMask *changes, double scale) {
    double strength = (double)abs(delta) * scale;
    InputValue value(true, strength, time);
    InputValue cancelValue(false, strength, time);

    bool changed = false;
    changed |= ImplProcessInput(&axis.Forward, delta > 0 ? value : cancelValue, changes, true);
    changed |= ImplProcessInput(&axis.Backward, delta < 0 ? value : cancelValue, changes, true);
    return changed;
}

static bool ImplMouseMotionHook(int dx, int dy, DWORD time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = G.Mouse.Motion;

    bool processed = false;
    processed |= ImplProcessMouseAxis(input.Horz, dx, time, changes, 1.0);
    processed |= ImplProcessMouseAxis(input.Vert, -dy, time, changes, 1.0);
    return processed;
}

static bool ImplMouseWheelHook(bool horiz, int delta, DWORD time, ChangedMask *changes) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = horiz ? G.Mouse.HWheel : G.Mouse.Wheel;

    constexpr double scale = (double)1 / WHEEL_DELTA;
    return ImplProcessMouseAxis(input, delta, time, changes, scale);
}

static void ImplOnCustomKey(int index, const InputValue &value) {
    DBG_ASSERT_DLL_THREAD();

    ChangedMask changes;
    if ((size_t)index < G.CustomKeys.size()) {
        auto customKey = G.CustomKeys[index].get();

        if (!ImplProcessInput(&customKey->Key, value, &changes)) {
            if (customKey->Callback) {
                customKey->Callback(value, nullptr);
            }
        }
    }
}

static bool ImplCheckInput(ImplInput *input, bool down) {
    if (G.Paused) {
        return false;
    }

    if (down) {
        input->ObservedPressForCheck = true;
    } else if (!input->ObservedPressForCheck) {
        return false; // forward releases of presses started while inactive
    }

    for (ImplMapping *mapping = input->Mappings; mapping; mapping = mapping->Next) {
        if (ImplCanProcess(mapping, down, !down, true) &&
            !mapping->Forward && !G.Forward) {
            return true;
        }
    }

    return false;
}

static bool ImplCheckMouseButton(int virtKeyCode, bool down) {
    DBG_ASSERT_DLL_THREAD();

    virtKeyCode = ImplUnextend(virtKeyCode, false);
    ImplInput *input = G.Keyboard.Get(virtKeyCode);
    return input ? ImplCheckInput(input, down) : false;
}

static bool ImplCheckMouseMotion() {
    DBG_ASSERT_DLL_THREAD();

    auto &input = G.Mouse.Motion;

    bool processed = false;
    for (auto &input : {&input.Horz.Backward, &input.Horz.Forward, &input.Vert.Backward, &input.Vert.Forward}) {
        processed |= ImplCheckInput(input, true);
    }
    return processed;
}

static bool ImplCheckMouseWheel(bool horiz) {
    DBG_ASSERT_DLL_THREAD();

    auto &input = horiz ? G.Mouse.HWheel : G.Mouse.Wheel;

    bool processed = false;
    for (auto &input : {&input.Backward, &input.Forward}) {
        processed |= ImplCheckInput(input, true);
    }
    return processed;
}

static void ImplAbortMappings() {
    InputValue value(false, 0.0, GetTickCount());
    ChangedMask changes;

    for (int key = 0; key < ImplKeyboard::Count; key++) {
        auto &input = G.Keyboard.Keys[key];

        ImplProcessInput(&input, value, &changes, false, true);
    }

    // this leaves AsyncState wrong, up to UpdateAll to fix it
}

static void ImplBeginObservePresses() {
    for (int key = 0; key < ImplKeyboard::Count; key++) {
        auto &input = G.Keyboard.Keys[key];

        // ensure future releases get forwarded no matter what
        input.ObservedPress = input.ObservedPressForCheck = false;
    }
}

static void ImplToggleDisable() {
    ImplAbortMappings();
    G.Disable = !G.Disable;
    UpdateAll();
}

static void ImplUpdateActive(bool oldActive, bool allowUpdateAll = true) {
    bool newActive = G.IsActive();

    if (oldActive && !newActive) {
        ImplAbortMappings();
    } else if (newActive && !oldActive) {
        ImplBeginObservePresses();
    }

    G.Paused = !newActive;

    if (oldActive != newActive && allowUpdateAll) {
        UpdateAll();
    }
}

static void ImplToggleAlways() {
    bool oldActive = G.IsActive();
    G.Always = !G.Always;
    ImplUpdateActive(oldActive);
}

static void ImplToggleForeground(bool allowUpdateAll) {
    bool oldActive = G.IsActive();
    G.InForeground = !G.InForeground;
    ImplUpdateActive(oldActive, allowUpdateAll);
}

static void ImplToggleConnected(void *userIndex) {
    auto user = &G.Users[(int)(uintptr_t)userIndex];

    user->Connected = !user->Connected;
    G.GlobalCallbacks.Call(user, user->Connected, false);
}
