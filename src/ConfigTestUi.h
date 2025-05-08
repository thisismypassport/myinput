#pragma once
#include "CommonUi.h"
#include "MyInputHook.h"
#include "State.h"
#include "Keys.h"
#include <hidusage.h>
#define IMPL_KEY_MOUSE_UTILS_ONLY
#include "ImplKeyMouse.h"
#include <map>

struct TestSource {
    MyVkSource Source;
    int Idx;

    TestSource(MyVkSource src, int idx = -1) : Source(src), Idx(idx) {}
    auto operator<=>(const TestSource &) const = default;

    wstring GetText() {
        switch (Source) {
        case MyVkSource::Keyboard:
            return L"Keyboard:";
        case MyVkSource::Mouse:
            return L"Mouse:";
        case MyVkSource::Pad:
            return L"Gamepad #" + StrFromValue<wchar_t>(Idx) + L":";
        default:
            return L"???";
        }
    }
};

struct TestKey {
    enum CtrlType {
        Bool,
        Strength,
        Stick,
        Count,
    };

    TestSource Src;
    int Vk;
    CtrlType Type;

    TestKey(TestSource src, int vk, CtrlType type) : Src(src), Vk(vk), Type(type) {}
    auto operator<=>(const TestKey &) const = default;

    const wchar_t *GetText() {
        const wchar_t *text = L"???";
        switch (Vk) {
#define CONFIG_ON_KEY(vk, pname, desc, group, name, ...) \
    case vk:                                             \
        text = desc;                                     \
        break;
            ENUMERATE_KEYS_WITH_SIMPLE(CONFIG_ON_KEY);
#undef CONFIG_ON_KEY
        }
        return text;
    }
};

class CheckButtonWithSlider : public CheckButton {
    Slider *mSlider = nullptr;

public:
    CheckButtonWithSlider(HWND p, intptr_t i, const wchar_t *text, Slider *slider) : CheckButton(p, i, text), mSlider(slider) {}

    void Set(bool value, int scale) {
        CheckButton::Set(value);
        mSlider->Set(scale);
    }
};

class StickDrawing : public DrawingBase {
    double mX = 0, mY = 0;
    DrawCtxt::Pen mBgPen = DrawCtxt::Pen(1, RGB(0, 0, 0));
    DrawCtxt::Pen mFgPen = DrawCtxt::Pen(2, RGB(0xff, 0, 0));

public:
    using DrawingBase::DrawingBase;

    SIZE Size() override { return {32, 32}; }

    void Draw(DrawCtxt &draw, RECT rect) override {
        DrawCtxt::Set bg(draw, mBgPen);
        draw.Rectangle(rect);
        draw.Ellipse(rect);

        DrawCtxt::Set fg(draw, mFgPen);
        POINT center = RectCenter(rect);
        POINT dest{center.x + (LONG)(mX * RectWidth(rect) / 2),
                   center.y + (LONG)(-mY * RectHeight(rect) / 2)};
        draw.Line(center, dest);
    }

    void Set(double x, double y) {
        mX = x, mY = y;
        Invalidate();
    }
};

class InputScrollPanel : public ScrollPanel {
    Layout *mBaseLayout;
    std::map<TestSource, WrapLayout *> mSourceLayouts;
    std::map<TestKey, Control *> mControls;
    PassiveTimer mLastTime;

    WrapLayout *GetOrAddLayout(TestSource src) {
        auto &layoutRef = mSourceLayouts[src];
        if (!layoutRef) {
            layoutRef = New<WrapLayout>();

            auto parentLay = New<Layout>();
            parentLay->AddTop(New<Separator>());
            parentLay->AddLeftMiddle(New<Label>(src.GetText().c_str()));
            parentLay->AddRemaining(layoutRef);
            mBaseLayout->AddTop(parentLay, -1, 15);
        }
        return layoutRef;
    }

public:
    using ScrollPanel::ScrollPanel;
    ScrollType GetScrollType() override { return ScrollType::Vert; }
    bool CanWrap() override { return true; }

    Control *OnCreate() override {
        mBaseLayout = New<Layout>();

        auto overlay = New<Overlay>();
        overlay->Add(mBaseLayout);
        overlay->Add(New<Panel>()); // to prevent input
        return overlay;
    }

    void Clear() {
        mSourceLayouts.clear();
        mControls.clear();
        mBaseLayout = nullptr;
        ResetChildren();
    }

    void AddBool(TestSource src, int vk, bool value) {
        TestKey key(src, vk, TestKey::Bool);
        auto &ctrlRef = mControls[key];

        if (!ctrlRef) {
            if (!value) {
                return;
            }

            auto layout = GetOrAddLayout(src);
            ctrlRef = New<CheckButton>(key.GetText());
            layout->Add(ctrlRef);
        }

        ((CheckButton *)ctrlRef)->Set(value);
    }

    void AddStrength(TestSource src, int vk, bool value, double strength) {
        TestKey key(src, vk, TestKey::Strength);
        auto &ctrlRef = mControls[key];

        const int scale = 100;
        if (!ctrlRef) {
            if (!value && !strength) {
                return;
            }

            auto layout = GetOrAddLayout(src);
            auto ctrlLay = New<Layout>(SIZE{});
            auto slider = New<Slider>(scale);
            ctrlRef = New<CheckButtonWithSlider>(key.GetText(), slider);
            ctrlLay->AddLeftMiddle(slider, 50);
            ctrlLay->AddLeftMiddle(ctrlRef);
            layout->Add(ctrlLay);
        }

        ((CheckButtonWithSlider *)ctrlRef)->Set(value, (int)(strength * scale));
    }

    void AddStick(TestSource src, int vk, const wchar_t *text, double x, double y) {
        TestKey key(src, vk, TestKey::Stick);
        auto &ctrlRef = mControls[key];

        if (!ctrlRef) {
            if (!x && !y) {
                return;
            }

            auto layout = GetOrAddLayout(src);
            auto ctrlLay = New<Layout>(SIZE{});
            ctrlRef = New<StickDrawing>();
            ctrlLay->AddLeftMiddle(ctrlRef);
            ctrlLay->AddLeftMiddle(New<Label>(text));
            layout->Add(ctrlLay);
        }

        ((StickDrawing *)ctrlRef)->Set(x, y);
    }

    void AddCount(TestSource src, int vk, const wchar_t *text = NULL) {
        TestKey key(src, vk, TestKey::Count);
        auto &ctrlRef = mControls[key];

        if (!ctrlRef) {
            auto layout = GetOrAddLayout(src);
            auto ctrlLay = New<Layout>(SIZE{});
            ctrlRef = New<EditInt<int>>(0);
            ctrlLay->AddLeftMiddle(ctrlRef, 40);
            ctrlLay->AddLeftMiddle(New<Label>(text ? text : key.GetText()));
            layout->Add(ctrlLay);
        }

        ((EditInt<int> *)ctrlRef)->Add(1);
    }
};

class ErrorScrollPanel : public ScrollPanel {
    wstring mErrors;
    StyledLabel *mErrorsLbl = nullptr;

public:
    using ScrollPanel::ScrollPanel;
    ScrollType GetScrollType() override { return ScrollType::Vert; }
    SIZE GetMaxSize() override { return {0, 100}; }

    Control *OnCreate() override {
        mErrorsLbl = New<StyledLabel>();
        return mErrorsLbl;
    }

    void Reset() {
        mErrors.clear();
        mErrorsLbl->SetText(L"Config is OK");
        mErrorsLbl->SetColor(RGB(0, 0x80, 0));
    }

    void Add(const wstring &error) {
        mErrors += error;
        mErrorsLbl->SetText(mErrors.c_str());
        mErrorsLbl->SetColor(RGB(0xff, 0, 0));
    }
};

class ConfigTestPanel : public Panel {
    struct UserState {
        void *Callback = nullptr;
        MyInputHook_InState_Basic State = {};
        MyInputHook_InState_Motion Motion = {};
    };

    ConfigUiIntf *mConfigIntf;
    ConfigChoicePanel *mConfigChoice = nullptr;
    Button *mClearBtn = nullptr;
    InputScrollPanel *mInputs = nullptr;
    ErrorScrollPanel *mErrorsScroll = nullptr;
    bool mInitialized = false;
    bool mRegistered = false;
    HANDLE mCallbackEvent = nullptr;
    UserState mUsers[IMPL_MAX_USERS] = {};

    template <typename TCb>
    void PostInMyInputThreadBlocking(TCb &&cb) {
        if (!mCallbackEvent) {
            mCallbackEvent = CreateEventW(nullptr, false, false, nullptr);
        }

        tuple<HANDLE, TCb *> data = {mCallbackEvent, &cb};

        MyInputHook_PostInDllThread([](void *vdata) {
            auto [event, cbPtr] = *(tuple<HANDLE, TCb *> *)vdata;
            (*cbPtr)();
            SetEvent(event);
        },
                                    &data);

        WaitForSingleObject(mCallbackEvent, INFINITE);
    }

    void LoadConfig(const wchar_t *config) {
        Post([=] { mErrorsScroll->Reset(); }); // post to sync with OnMyLog

        PostInMyInputThreadBlocking([&] {
            MyInputHook_SetLogCallback(OnMyLog, this);
            MyInputHook_LoadConfig(config);
        });
    }

    void RegisterRawInput(bool enable) {
        RAWINPUTDEVICE rawDevs[2] = {};
        rawDevs[0].usUsagePage = rawDevs[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevs[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
        rawDevs[1].usUsage = HID_USAGE_GENERIC_MOUSE;
        rawDevs[0].hwndTarget = rawDevs[1].hwndTarget = enable ? mControl : nullptr;
        rawDevs[0].dwFlags = rawDevs[1].dwFlags = enable ? RIDEV_INPUTSINK : RIDEV_REMOVE;

        BOOL ok = RegisterRawInputDevices(rawDevs, 2, sizeof(RAWINPUTDEVICE));
        ASSERT(ok, "Failed registering raw key/mouse");
    }

    void Register(bool regEnable) {
        if (regEnable && !mRegistered) {
            if (!mInitialized) {
                SetEnvironmentVariableW(L"MYINPUT_HOOK_CONFIG", L"<empty>");
                if (!LoadLibraryA("myinput_hook.dll")) {
                    return Alert(L"Failed loading myinput");
                }

                MyInputHook_WaitInit();
                mInitialized = true;
            }

            RegisterRawInput(true);

            LoadConfig(mConfigChoice->GetSelected());

            PostInMyInputThreadBlocking([&] {
                for (int i = 0; i < IMPL_MAX_USERS; i++) {
                    mUsers[i].Callback = MyInputHook_RegisterCallback(i, OnMyDeviceKey, this);
                }
            });

            mRegistered = true;
        } else if (!regEnable && mRegistered) {
            RegisterRawInput(false);

            PostInMyInputThreadBlocking([&] {
                MyInputHook_SetLogCallback(nullptr, nullptr);

                for (int i = 0; i < IMPL_MAX_USERS; i++) {
                    mUsers[i].Callback = MyInputHook_UnregisterCallback(i, mUsers[i].Callback);
                }

                MyInputHook_LoadConfig(L"<empty>");
            });

            mRegistered = false;
        }
    }

    void OnActivate(bool activate, void *root, void *prev) override {
        if (root) // only when switching tabs
        {
            if (activate) {
                mConfigChoice->SetSelected(mConfigIntf->GetConfig(), false);
                Register(true);
            } else {
                Register(false);
                mInputs->Clear();
            }
        }
    }

    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        if (msg == WM_INPUT) {
            RAWINPUT input;
            UINT inSize = sizeof input;
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &input, &inSize, sizeof(RAWINPUTHEADER))) {
                switch (input.header.dwType) {
                case RIM_TYPEKEYBOARD:
                    OnKeyboardKey(&input.data.keyboard);
                    break;
                case RIM_TYPEMOUSE:
                    OnMouseKey(&input.data.mouse);
                    break;
                }
            }
        }

        return Panel::OnMessage(hwnd, msg, wParam, lParam);
    }

    void OnKeyboardKey(RAWKEYBOARD *key) {
        int vk = key->VKey;
        int flags = key->Flags;

        ImplSplitLeftRight(&vk, &flags, RI_KEY_E0, key->MakeCode);
        vk = ImplUnextend(vk, flags & (RI_KEY_E0 | RI_KEY_E1));

        mInputs->AddBool(TestSource(MyVkSource::Keyboard), vk, !(flags & RI_KEY_BREAK));
    }

    void OnMouseKey(RAWMOUSE *mouse) {
        TestSource src(MyVkSource::Mouse);

        if (mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
            mInputs->AddBool(src, VK_LBUTTON, true);
        }
        if (mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) {
            mInputs->AddBool(src, VK_LBUTTON, false);
        }
        if (mouse->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
            mInputs->AddBool(src, VK_RBUTTON, true);
        }
        if (mouse->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
            mInputs->AddBool(src, VK_RBUTTON, false);
        }
        if (mouse->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
            mInputs->AddBool(src, VK_MBUTTON, true);
        }
        if (mouse->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) {
            mInputs->AddBool(src, VK_MBUTTON, false);
        }
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) {
            mInputs->AddBool(src, VK_XBUTTON1, true);
        }
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_4_UP) {
            mInputs->AddBool(src, VK_XBUTTON1, false);
        }
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) {
            mInputs->AddBool(src, VK_XBUTTON2, true);
        }
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_5_UP) {
            mInputs->AddBool(src, VK_XBUTTON2, false);
        }

        if (mouse->usButtonFlags & RI_MOUSE_WHEEL) {
            mInputs->AddCount(src, (SHORT)mouse->usButtonData > 0 ? MY_VK_WHEEL_UP : MY_VK_WHEEL_DOWN);
        }
        if (mouse->usButtonFlags & RI_MOUSE_HWHEEL) {
            mInputs->AddCount(src, (SHORT)mouse->usButtonData > 0 ? MY_VK_WHEEL_RIGHT : MY_VK_WHEEL_LEFT);
        }
    }

    void OnMyDeviceKey(int idx, const MyInputHook_InState_Basic &state, const MyInputHook_InState_Motion &motion) {
        TestSource src(MyVkSource::Pad, idx);

        auto &user = mUsers[idx];
        if (state.A != user.State.A) {
            mInputs->AddBool(src, MY_VK_PAD_A, state.A);
        }
        if (state.B != user.State.B) {
            mInputs->AddBool(src, MY_VK_PAD_B, state.B);
        }
        if (state.X != user.State.X) {
            mInputs->AddBool(src, MY_VK_PAD_X, state.X);
        }
        if (state.Y != user.State.Y) {
            mInputs->AddBool(src, MY_VK_PAD_Y, state.Y);
        }
        if (state.Start != user.State.Start) {
            mInputs->AddBool(src, MY_VK_PAD_START, state.Start);
        }
        if (state.Back != user.State.Back) {
            mInputs->AddBool(src, MY_VK_PAD_BACK, state.Back);
        }
        if (state.Guide != user.State.Guide) {
            mInputs->AddBool(src, MY_VK_PAD_GUIDE, state.Guide);
        }
        if (state.Extra != user.State.Extra) {
            mInputs->AddBool(src, MY_VK_PAD_EXTRA, state.Extra);
        }
        if (state.DL != user.State.DL) {
            mInputs->AddBool(src, MY_VK_PAD_DPAD_LEFT, state.DL);
        }
        if (state.DR != user.State.DR) {
            mInputs->AddBool(src, MY_VK_PAD_DPAD_RIGHT, state.DR);
        }
        if (state.DU != user.State.DU) {
            mInputs->AddBool(src, MY_VK_PAD_DPAD_UP, state.DU);
        }
        if (state.DD != user.State.DD) {
            mInputs->AddBool(src, MY_VK_PAD_DPAD_DOWN, state.DD);
        }
        if (state.LB != user.State.LB) {
            mInputs->AddBool(src, MY_VK_PAD_LSHOULDER, state.LB);
        }
        if (state.RB != user.State.RB) {
            mInputs->AddBool(src, MY_VK_PAD_RSHOULDER, state.RB);
        }
        if (state.L != user.State.L) {
            mInputs->AddBool(src, MY_VK_PAD_LTHUMB_PRESS, state.L);
        }
        if (state.R != user.State.R) {
            mInputs->AddBool(src, MY_VK_PAD_RTHUMB_PRESS, state.R);
        }

        if (state.LTStr != user.State.LTStr || state.LT != user.State.LT) {
            mInputs->AddStrength(src, MY_VK_PAD_LTRIGGER, state.LT, state.LTStr);
        }
        if (state.RTStr != user.State.RTStr || state.RT != user.State.RT) {
            mInputs->AddStrength(src, MY_VK_PAD_RTRIGGER, state.RT, state.RTStr);
        }

        if (state.LX != user.State.LX || state.LY != user.State.LY) {
            mInputs->AddStick(src, MY_VK_PAD_LTHUMB_UP, L"Gamepad Left Stick", state.LX, state.LY);
        }
        if (state.RX != user.State.RX || state.RY != user.State.RY) {
            mInputs->AddStick(src, MY_VK_PAD_RTHUMB_UP, L"Gamepad Right Stick", state.RX, state.RY);
        }

        if (motion.X.Pos != user.Motion.X.Pos || motion.Y.Pos != user.Motion.Y.Pos || motion.Z.Pos != user.Motion.Z.Pos ||
            motion.RX.Pos != user.Motion.RX.Pos || motion.RY.Pos != user.Motion.RY.Pos || motion.RZ.Pos != user.Motion.RZ.Pos) {
            mInputs->AddCount(src, MY_VK_PAD_MOTION_FAR, L"Gamepad Motion...");
        }

        user.State = state;
        user.Motion = motion;
    }

    static void OnMyDeviceKey(int idx, void *data) {
        MyInputHook_InState_Basic state;
        MyInputHook_InState_Motion motion;
        MyInputHook_GetInState(idx, MyInputHook_InState_Basic_Type, &state, sizeof state);
        MyInputHook_GetInState(idx, MyInputHook_InState_Motion_Type, &motion, sizeof motion);

        auto panel = (ConfigTestPanel *)data;
        panel->Post([=] { panel->OnMyDeviceKey(idx, state, motion); });
    }

    void OnMyLog(const wstring &wstr) {
        mErrorsScroll->Add(wstr);
    }

    static void OnMyLog(const char *str, size_t size, char levelCh, void *data) {
        LogLevel level = (LogLevel)levelCh;
        if (level == LogLevel::Warning || level == LogLevel::Error) {
            auto wstr = ToStdWStr(string_view(str, size));
            auto panel = (ConfigTestPanel *)data;
            panel->Post([=] { panel->OnMyLog(wstr); });
        }
    }

public:
    ConfigTestPanel(HWND parent, intptr_t id, ConfigUiIntf *cfgUi) : Panel(parent, id), mConfigIntf(cfgUi) {}

    Control *OnCreate() override {
        mConfigChoice = New<ConfigChoicePanel>(CONFIG_CHOICE_ALLOW_NONE, [this](Path &&config) {
            mConfigIntf->SetConfig(config);
            LoadConfig(config);
            mInputs->Clear();
            mInputs->SetFocus();
        });

        mConfigChoice->SetOnOpen([this](bool open) {
            if (open) {
                Register(false);
            } else {
                Register(true);
                mInputs->Clear();
            }
        });

        mClearBtn = New<Button>(L"(Clear)", [this] {
            mInputs->Clear();
            mInputs->SetFocus();
        });

        mInputs = New<InputScrollPanel>();
        mErrorsScroll = New<ErrorScrollPanel>();

        auto headerLay = New<Layout>();
        headerLay->AddLeftMiddle(mClearBtn);
        headerLay->AddLeftMiddle(New<Label>(L"Recent inputs:"));

        auto layout = New<Layout>(true);
        layout->AddTopMiddle(mConfigChoice);
        layout->AddTop(New<Separator>());
        layout->AddTop(headerLay, -1, 0, 20);
        layout->AddBottom(mErrorsScroll);
        layout->AddRemaining(mInputs);
        return layout;
    }

    Control *InitialFocus() override { return this; }
};
