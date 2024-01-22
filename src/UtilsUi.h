#pragma once
#include "UtilsUiBase.h"
#include <CommCtrl.h>

class IControl {
public:
    virtual SIZE GetIdealSize() = 0; // (should be cached!)
    virtual void SetPosition(RECT rect) = 0;
    virtual void OnCommand(WORD value) {}

    void OnAdd() {} // called via template
};

class Control : public IControl {
protected:
    HWND mControl = nullptr;

    void InitFont() {
        SendMessageW(mControl, WM_SETFONT, (LPARAM)GetStockObject(DEFAULT_GUI_FONT), false);
    }

    void InitCommon(DWORD flag) {
        INITCOMMONCONTROLSEX info = {};
        info.dwSize = sizeof(info);
        info.dwICC = flag;
        InitCommonControlsEx(&info);
    }

    SIZE DialogUnitsToSize(SIZE units) {
        HDC dc = GetDC(mControl);
        HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW metrics = {};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, oldFont);
        ReleaseDC(mControl, dc);

        LONG cx = MulDiv(metrics.tmAveCharWidth, units.cx, 4);
        LONG cy = MulDiv(metrics.tmHeight, units.cy, 8);
        return {cx, cy};
    }

    SIZE GetWindowTextSize() {
        int length = GetWindowTextLengthW(mControl);

        Path text(length + 1);
        GetWindowTextW(mControl, text, length + 1);

        RECT rect = {};
        HDC dc = GetDC(mControl);
        HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc, text, length, &rect, DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, oldFont);
        ReleaseDC(mControl, dc);

        return {rect.right - rect.left, rect.bottom - rect.top};
    }

    friend class PanelBase;

public:
    virtual ~Control() { DestroyWindow(mControl); }

    SIZE GetIdealSize() override { return SIZE(100, 100); } // better than nothing

    void Show(bool show) { ShowWindow(mControl, show ? SW_SHOWNOACTIVATE : SW_HIDE); }
    void Hide() { Show(false); }
    void Enable(bool enable) { EnableWindow(mControl, enable); }
    void Disable() { Enable(false); }
    void Focus() { SetFocus(mControl); }

    RECT GetPosition() {
        RECT rect = {};
        GetWindowRect(mControl, &rect);
        return rect;
    }

    void SetPosition(RECT rect) override {
        MoveWindow(mControl, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, false);
    }
};

class Label : public Control {
    SIZE mIdealSize = {-1, -1};

public:
    Label(HWND parent, intptr_t id, const wchar_t *text) {
        mControl = CreateWindowW(WC_STATICW, text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    SIZE GetIdealSize() override {
        if (mIdealSize.cx < 0) {
            mIdealSize = GetWindowTextSize();
        }
        return mIdealSize;
    }
};

class ButtonBase : public Control {
    function<void()> mAction;

public:
    ButtonBase(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action, int styles) : mAction(move(action)) {
        mControl = CreateWindowW(WC_BUTTONW, text, WS_TABSTOP | WS_VISIBLE | WS_CHILD | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    void OnCommand(WORD value) override {
        mAction();
    }
};

class Button : public ButtonBase {
    SIZE mIdealSize = {-1, -1};

public:
    Button(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action) : ButtonBase(parent, id, text, move(action), BS_PUSHBUTTON | BS_MULTILINE) {}

    SIZE GetIdealSize() override {
        if (mIdealSize.cx < 0) {
            SIZE size = {};
            SendMessageW(mControl, BCM_GETIDEALSIZE, 0, (LPARAM)&size);
            mIdealSize = size;
        }
        return mIdealSize;
    }
};

class CheckBase : public Control {
    function<void(bool value)> mAction;
    SIZE mIdealSize = {-1, -1};

public:
    CheckBase(HWND parent, intptr_t id, const wchar_t *text, function<void(bool)> &&action, int styles) : mAction(move(action)) {
        mControl = CreateWindowW(WC_BUTTONW, text, WS_TABSTOP | WS_VISIBLE | WS_CHILD | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    bool Get() { return SendMessageW(mControl, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void Set(bool value = true) { SendMessageW(mControl, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
    void Clear() { Set(false); }

    void OnCommand(WORD value) override {
        mAction(Get());
    }

    SIZE GetIdealSize() override {
        if (mIdealSize.cx < 0) {
            mIdealSize = GetWindowTextSize();
            mIdealSize.cx += DialogUnitsToSize({0, 11}).cy;                   // ??? (12?)
            mIdealSize.cy = max(mIdealSize.cy, DialogUnitsToSize({0, 9}).cy); // ??? (10?)
        }
        return mIdealSize;
    }
};

class CheckBox : public CheckBase {
public:
    CheckBox(HWND parent, intptr_t id, const wchar_t *text, function<void(bool)> &&action) : CheckBase(parent, id, text, move(action), BS_AUTOCHECKBOX | BS_MULTILINE) {}
};

class CheckButton : public CheckBase {
public:
    CheckButton(HWND parent, intptr_t id, const wchar_t *text, function<void(bool)> &&action) : CheckBase(parent, id, text, move(action), BS_AUTOCHECKBOX | BS_MULTILINE | BS_PUSHLIKE) {}
};

template <class TValue>
class RadioGroup : public IControl {
    function<void(const TValue &)> mAction;
    bool mEmpty = true;

protected:
    virtual SIZE GetIdealSize() { return {}; }
    virtual void SetPosition(RECT rect) {}

public:
    RadioGroup(HWND, intptr_t, function<void(const TValue &)> &&action) : mAction(move(action)) {}

    bool AssignIsFirst() {
        bool first = mEmpty;
        mEmpty = false;
        return first;
    }

    void Act(const TValue &value) {
        mAction(value);
    }
};

class RadioBase : public ButtonBase {
public:
    using ButtonBase::ButtonBase;

    bool Get() { return SendMessageW(mControl, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void Set(bool value = true) { SendMessageW(mControl, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
    void Clear() { Set(false); }
};

class RadioBox : public RadioBase {
public:
    RadioBox(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action, bool first = false) : RadioBase(parent, id, text, move(action), BS_AUTORADIOBUTTON | BS_MULTILINE | (first ? WS_GROUP : 0)) {}

    template <class TValue>
    RadioBox(HWND parent, intptr_t id, const wchar_t *text, RadioGroup<TValue> *group, const TValue &value) : RadioBox(
                                                                                                                  parent, id, text, [=] { group->Act(value); }, group->AssignIsFirst()) {}
};

class RadioButton : public RadioBase {
public:
    RadioButton(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action, bool first = false) : RadioBase(parent, id, text, move(action), BS_AUTORADIOBUTTON | BS_MULTILINE | BS_PUSHLIKE | (first ? WS_GROUP : 0)) {}

    template <class TValue>
    RadioButton(HWND parent, intptr_t id, const wchar_t *text, RadioGroup<TValue> *group, const TValue &value) : RadioButton(
                                                                                                                     parent, id, text, [=] { group->Act(value); }, group->AssignIsFirst()) {}
};

class EditBase : public Control {
    function<void(bool done)> mChanged;
    bool mHasChanges = false;

public:
    EditBase(HWND parent, intptr_t id, const wchar_t *text, function<void(bool done)> &&changed, int styles) : mChanged(move(changed)) {
        mControl = CreateWindowW(WC_EDITW, text,
                                 WS_TABSTOP | WS_CHILD | WS_VISIBLE | WS_BORDER | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    Path Get() {
        int size = GetWindowTextLengthW(mControl) + 1;

        Path text(size);
        GetWindowTextW(mControl, text, size);
        return text;
    }

    void Set(const wchar_t *value) {
        SetWindowTextW(mControl, value);
        mHasChanges = false;
    }

    void OnCommand(WORD value) override {
        if (value == EN_CHANGE) {
            mChanged(false), mHasChanges = true;
        } else if (value == EN_KILLFOCUS && mHasChanges) {
            mChanged(true), mHasChanges = false;
        }
    }
};

class EditLine : public EditBase {
    LONG mIdealHeight = -1;

public:
    EditLine(HWND parent, intptr_t id, const wchar_t *text, function<void(bool done)> &&changed) : EditBase(parent, id, text, move(changed), ES_AUTOHSCROLL | ES_LEFT | ES_NOHIDESEL) {}

    EditLine(HWND parent, intptr_t id, function<void(bool done)> &&changed) : EditLine(parent, id, nullptr, move(changed)) {}

    SIZE GetIdealSize() override {
        if (mIdealHeight < 0) {
            mIdealHeight = DialogUnitsToSize({0, 11}).cy; // ??? (14?)
        }
        return SIZE{100, mIdealHeight};
    }
};

class EditBox : public EditBase {
public:
    EditBox(HWND parent, intptr_t id, const wchar_t *text, function<void(bool done)> &&changed) : EditBase(parent, id, text, move(changed), ES_AUTOVSCROLL | ES_LEFT | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL) {}

    EditBox(HWND parent, intptr_t id, function<void(bool done)> &&changed) : EditBox(parent, id, nullptr, move(changed)) {}
};

class ListBoxBase : public Control {
public:
    int Add(const wchar_t *str) { return (int)SendMessageW(mControl, LB_ADDSTRING, 0, (LPARAM)str); }
    void Insert(int idx, const wchar_t *str) { SendMessageW(mControl, LB_INSERTSTRING, idx, (LPARAM)str); }

    void Remove(int idx) { SendMessageW(mControl, LB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, LB_RESETCONTENT, 0, 0); }
};

class ListBox : public ListBoxBase {
    function<void(int)> mAction;

public:
    ListBox(HWND parent, intptr_t id, function<void(int idx)> &&action) : mAction(move(action)) {
        mControl = CreateWindowW(WC_LISTBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_VISIBLE | WS_BORDER |
                                     LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    int GetSelected() { return (int)SendMessageW(mControl, LB_GETCURSEL, 0, 0); }

    void SetSelected(int idx, bool action = true) {
        SendMessageW(mControl, LB_SETCURSEL, idx, 0);
        if (action) {
            mAction(idx);
        }
    }

    void OnCommand(WORD value) override {
        if (value == LBN_SELCHANGE) {
            mAction(GetSelected());
        }
    }
};

class MultiListBox : public ListBoxBase {
    function<void(const vector<int> &)> mAction;

public:
    MultiListBox(HWND parent, intptr_t id, function<void(const vector<int> &indices)> &&action) : mAction(move(action)) {
        mControl = CreateWindowW(WC_LISTBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_VISIBLE | WS_BORDER |
                                     LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    vector<int> GetSelected() {
        int count = (int)SendMessageW(mControl, LB_GETSELCOUNT, 0, 0);
        vector<int> data(count);
        SendMessageW(mControl, LB_GETSELITEMS, count, (LPARAM)data.data());
        return data;
    }

    void SetSelected(const vector<int> &indices, bool action = true) {
        SendMessageW(mControl, LB_SETSEL, FALSE, -1);
        for (int idx : indices) {
            SendMessageW(mControl, LB_SETSEL, TRUE, idx);
        }

        if (action) {
            mAction(indices);
        }
    }

    void OnCommand(WORD value) override {
        if (value == LBN_SELCHANGE) {
            mAction(GetSelected());
        }
    }
};

class DropDownList : public Control {
    function<void(int)> mAction;
    LONG mIdealHeight = -1;

public:
    DropDownList(HWND parent, intptr_t id, function<void(int idx)> &&action) : mAction(move(action)) {
        mControl = CreateWindowW(WC_COMBOBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    int Add(const wchar_t *str) { return (int)SendMessageW(mControl, CB_ADDSTRING, 0, (LPARAM)str); }
    void Insert(int idx, const wchar_t *str) { SendMessageW(mControl, CB_INSERTSTRING, idx, (LPARAM)str); }

    void Remove(int idx) { SendMessageW(mControl, CB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, CB_RESETCONTENT, 0, 0); }

    int GetSelected() { return (int)SendMessageW(mControl, CB_GETCURSEL, 0, 0); }

    void SetSelected(int idx, bool action = true) {
        SendMessageW(mControl, CB_SETCURSEL, idx, 0);
        if (action) {
            mAction(idx);
        }
    }

    void OnCommand(WORD value) override {
        if (value == CBN_SELCHANGE) {
            mAction(GetSelected());
        }
    }

    SIZE GetIdealSize() override {
        if (mIdealHeight < 0) {
            mIdealHeight = DialogUnitsToSize({0, 11}).cy; // ??? (14?)
        }
        return SIZE{100, mIdealHeight};
    }
};

class Tab : public Control {
    function<void(int)> mAction;
    UniquePtr<IControl> mChild;

public:
    Tab(HWND parent, intptr_t id, function<void(int idx)> &&action) : mAction(move(action)) {
        mControl = CreateWindowW(WC_TABCONTROLW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_MULTILINE,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    void Add(int idx, const wchar_t *str) {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = (wchar_t *)str;
        SendMessageW(mControl, TCM_INSERTITEMW, idx, (LPARAM)&item);
    }

    int Add(const wchar_t *str) {
        int count = (int)SendMessageW(mControl, TCM_GETITEMCOUNT, 0, 0);
        Add(count, str);
        return count;
    }

    void Remove(int idx) { SendMessageW(mControl, TCM_DELETEITEM, idx, 0); }
    void Clear() { SendMessageW(mControl, TCM_DELETEALLITEMS, 0, 0); }

    int GetSelected() { return (int)SendMessageW(mControl, TCM_GETCURSEL, 0, 0); }

    void SetSelected(int idx, bool action = true) {
        SendMessageW(mControl, TCM_SETCURSEL, idx, 0);
        if (action) {
            mAction(idx);
        }
    }

    void OnCommand(WORD value) override {
        if (value == TCN_SELCHANGE) {
            mAction(GetSelected());
        }
    }
};

class Layout : public IControl {
    enum class LayoutOp {
        OuterMargins,
        OnLeft,
        OnRight,
        OnTop,
        OnBottom,
        OnRest,
    };

    struct LayoutInstr {
        LayoutOp Op;
        IControl *Target;
        LONG Size, Offset, Margin;
    };

    struct LayoutArgs {
        RECT Rest;
        bool SetPos;
        // for !setPos case:
        RECT LastMargin;
        bool NeedMargin;
        SIZE Max;
    };

    vector<LayoutInstr> mInstrs;
    SIZE mMargin;
    SIZE mIdealSize = {-1, -1};

    void DoInstrs(LayoutArgs &args) {
        for (auto &instr : mInstrs) {
            switch (instr.Op) {
            case LayoutOp::OuterMargins:
                DoOuterMargins(args);
                break;
            case LayoutOp::OnLeft:
                DoOnLeft(instr, args);
                break;
            case LayoutOp::OnRight:
                DoOnRight(instr, args);
                break;
            case LayoutOp::OnTop:
                DoOnTop(instr, args);
                break;
            case LayoutOp::OnBottom:
                DoOnBottom(instr, args);
                break;
            case LayoutOp::OnRest:
                DoOnRest(instr, args);
                break;
            }
        }
    }

    void DoOuterMargins(LayoutArgs &a) {
        a.Rest.left += mMargin.cx;
        a.Rest.top += mMargin.cy;
        a.Rest.right -= mMargin.cx;
        a.Rest.bottom -= mMargin.cy;
        a.Rest.right = max(a.Rest.right, a.Rest.left);
        a.Rest.bottom = max(a.Rest.bottom, a.Rest.top);

        if (!a.SetPos) {
            a.LastMargin = {mMargin.cx, mMargin.cy, mMargin.cx, mMargin.cy};
        }
    }

    void DoOnTop(LayoutInstr i, LayoutArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cy;
        }
        if (i.Size < 0) {
            i.Size = i.Target->GetIdealSize().cy;
        }
        i.Size = min(i.Size, a.Rest.bottom - a.Rest.top);

        if (i.Target) {
            if (a.SetPos) {
                i.Target->SetPosition(RECT{a.Rest.left + i.Offset, a.Rest.top, a.Rest.right - i.Offset, a.Rest.top + i.Size});
            } else {
                a.Max.cx = max(a.Max.cx, i.Target->GetIdealSize().cx + i.Offset * 2);
            }
        }

        a.Rest.top += i.Size + i.Margin;
        a.Rest.top = min(a.Rest.top, a.Rest.bottom);

        if (!a.SetPos) {
            a.Max.cy -= i.Size + i.Margin;
            a.Max.cy = max(a.Max.cy, 0L);

            a.LastMargin.left = a.LastMargin.right = 0;
            a.LastMargin.top = i.Margin;
            a.NeedMargin = true;
        }
    }

    void DoOnBottom(LayoutInstr i, LayoutArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cy;
        }
        if (i.Size < 0) {
            i.Size = i.Target->GetIdealSize().cy;
        }
        i.Size = min(i.Size, a.Rest.bottom - a.Rest.top);

        if (i.Target) {
            if (a.SetPos) {
                i.Target->SetPosition(RECT{a.Rest.left + i.Offset, a.Rest.bottom - i.Size, a.Rest.right - i.Offset, a.Rest.bottom});
            } else {
                a.Max.cx = max(a.Max.cx, i.Target->GetIdealSize().cx + i.Offset * 2);
            }
        }

        a.Rest.bottom -= i.Size + i.Margin;
        a.Rest.bottom = max(a.Rest.bottom, a.Rest.top);

        if (!a.SetPos) {
            a.Max.cy -= i.Size + i.Margin;
            a.Max.cy = max(a.Max.cy, 0L);

            a.LastMargin.left = a.LastMargin.right = 0;
            a.LastMargin.bottom = i.Margin;
            a.NeedMargin = true;
        }
    }

    void DoOnLeft(LayoutInstr i, LayoutArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cx;
        }
        if (i.Size < 0) {
            i.Size = i.Target->GetIdealSize().cx;
        }
        i.Size = min(i.Size, a.Rest.right - a.Rest.left);

        if (i.Target) {
            if (a.SetPos) {
                i.Target->SetPosition(RECT{a.Rest.left, a.Rest.top + i.Offset, a.Rest.left + i.Size, a.Rest.bottom - i.Offset});
            } else {
                a.Max.cy = max(a.Max.cy, i.Target->GetIdealSize().cy + i.Offset * 2);
            }
        }

        a.Rest.left += i.Size + i.Margin;
        a.Rest.left = min(a.Rest.left, a.Rest.right);

        if (!a.SetPos) {
            a.Max.cx -= i.Size + i.Margin;
            a.Max.cx = max(a.Max.cx, 0L);

            a.LastMargin.top = a.LastMargin.bottom = 0;
            a.LastMargin.left = i.Margin;
            a.NeedMargin = true;
        }
    }

    void DoOnRight(LayoutInstr i, LayoutArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cx;
        }
        if (i.Size < 0) {
            i.Size = i.Target->GetIdealSize().cx;
        }
        i.Size = min(i.Size, a.Rest.right - a.Rest.left);

        if (i.Target) {
            if (a.SetPos) {
                i.Target->SetPosition(RECT{a.Rest.right - i.Size, a.Rest.top + i.Offset, a.Rest.right, a.Rest.bottom - i.Offset});
            } else {
                a.Max.cy = max(a.Max.cy, i.Target->GetIdealSize().cy + i.Offset * 2);
            }
        }

        a.Rest.right -= i.Size + i.Margin;
        a.Rest.right = max(a.Rest.right, a.Rest.left);

        if (!a.SetPos) {
            a.Max.cx -= i.Size + i.Margin;
            a.Max.cx = max(a.Max.cx, 0L);

            a.LastMargin.top = a.LastMargin.bottom = 0;
            a.LastMargin.right = i.Margin;
            a.NeedMargin = true;
        }
    }

    void DoOnRest(LayoutInstr i, LayoutArgs &a) {
        if (a.SetPos) {
            i.Target->SetPosition(RECT{a.Rest.left, a.Rest.top, a.Rest.right, a.Rest.bottom});

            a.Rest.right = a.Rest.left;
            a.Rest.bottom = a.Rest.top;
        } else {
            auto ideal = i.Target->GetIdealSize();
            a.Max.cx = max(a.Max.cx, ideal.cx);
            a.Max.cy = max(a.Max.cy, ideal.cy);
            a.LastMargin = {};
            a.NeedMargin = false;
        }
    }

public:
    Layout(HWND, intptr_t, bool outerMargins = false, SIZE margin = {8, 8}) : mMargin(margin) {
        if (outerMargins) {
            mInstrs.push_back({LayoutOp::OuterMargins});
        }
    }

    SIZE GetIdealSize() override {
        if (mIdealSize.cx < 0) {
            LayoutArgs args = {{0, 0, LONG_MAX, LONG_MAX}, false};
            DoInstrs(args);
            LONG extraMarginX = args.LastMargin.left + args.LastMargin.right - (args.NeedMargin ? max(args.LastMargin.left, args.LastMargin.right) : 0);
            LONG extraMarginY = args.LastMargin.top + args.LastMargin.bottom - (args.NeedMargin ? max(args.LastMargin.top, args.LastMargin.bottom) : 0);
            mIdealSize.cx = args.Rest.left + (LONG_MAX - args.Rest.right) + args.Max.cx - extraMarginX;
            mIdealSize.cy = args.Rest.top + (LONG_MAX - args.Rest.bottom) + args.Max.cy - extraMarginY;
        }
        return mIdealSize;
    }

    void SetPosition(RECT rect) override {
        LayoutArgs args = {rect, true};
        DoInstrs(args);
    }

    void OnTop(IControl *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) {
        mInstrs.push_back({LayoutOp::OnTop, ctrl, height, offset, margin});
    }
    void OnBottom(IControl *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) {
        mInstrs.push_back({LayoutOp::OnBottom, ctrl, height, offset, margin});
    }
    void OnLeft(IControl *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        mInstrs.push_back({LayoutOp::OnLeft, ctrl, width, offset, margin});
    }
    void OnRight(IControl *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        mInstrs.push_back({LayoutOp::OnRight, ctrl, width, offset, margin});
    }
    void OnRest(IControl *ctrl) {
        mInstrs.push_back({LayoutOp::OnRest, ctrl});
    }

    void OnTop(initializer_list<IControl *> ctrls, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        for (auto &ctrl : ctrls) {
            OnTop(ctrl, width, offset, margin);
        }
    }
    void OnBottom(initializer_list<IControl *> ctrls, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        for (auto &ctrl : ctrls) {
            OnBottom(ctrl, width, offset, margin);
        }
    }
    void OnLeft(initializer_list<IControl *> ctrls, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        for (auto &ctrl : ctrls) {
            OnLeft(ctrl, width, offset, margin);
        }
    }
    void OnRight(initializer_list<IControl *> ctrls, LONG width = -1, LONG offset = 0, LONG margin = -1) {
        for (auto &ctrl : ctrls) {
            OnRight(ctrl, width, offset, margin);
        }
    }
};

class PanelBase : public Control {
protected:
    vector<UniquePtr<IControl>> mChildren;
    IControl *mRootChild = nullptr;

    static LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        PanelBase *self;
        if (msg == WM_NCCREATE) {
            self = (PanelBase *)((CREATESTRUCT *)lParam)->lpCreateParams;
            self->mControl = hwnd; // before CreateWindow returns
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = (PanelBase *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!self) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
        }

        return self->OnMessage(hwnd, msg, wParam, lParam);
    }

    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE:
            mRootChild = OnCreate();
            break;
        case WM_DESTROY:
            OnDestroy();
            mControl = nullptr;
            break;

        case WM_SIZE:
            if (mRootChild) {
                mRootChild->SetPosition(RECT{0, 0, LOWORD(lParam), HIWORD(lParam)});
            }
            if (IsTopLevel()) {
                InvalidateRect(mControl, nullptr, true);
            }
            break;

        case WM_COMMAND:
            if (lParam) {
                WORD idx = LOWORD(wParam) - 1;
                if (idx < mChildren.size()) {
                    mChildren[idx]->OnCommand(HIWORD(wParam));
                }
            }
            break;

        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wParam, TRANSPARENT);
            return 0;

        case WM_APP:
            (*(function<void()> *)wParam)();
            if (lParam) {
                delete (function<void()> *)wParam;
            }
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RegisterWindowClass(const wchar_t *className) {
        WNDCLASSW wincls = {};
        wincls.lpszClassName = className;
        wincls.lpfnWndProc = WinProc;
        wincls.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wincls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wincls);
    }

    virtual bool IsTopLevel() { return false; }

public:
    // no ctor logic - need virtual functions!

    virtual IControl *OnCreate() { return nullptr; }
    virtual void OnDestroy() {}

    SIZE GetIdealSize() override {
        if (mRootChild) {
            return mRootChild->GetIdealSize();
        } else {
            return Control::GetIdealSize();
        }
    }

    template <class TControl, class... TArgs>
    TControl *Add(TArgs &&...args) {
        intptr_t id = mChildren.size() + 1;
        mChildren.emplace_back(UniquePtr<TControl>::New(mControl, id, forward<TArgs>(args)...));
        auto control = (TControl *)mChildren.back().get();
        control->OnAdd();
        return control;
    }

    void Post(function<void()> &&func) {
        PostMessageW(mControl, WM_APP, (WPARAM) new function<void()>(move(func)), true);
    }

    void Call(const function<void()> &func) {
        SendMessageW(mControl, WM_APP, (WPARAM)&func, false);
    }
};

class Panel : public PanelBase {
    HWND mParent;
    intptr_t mId;

protected:
    void OnAdd() {
        const wchar_t *className = L"MyPanel";

        static bool registered = false;
        if (!registered) {
            RegisterWindowClass(className);
            registered = true;
        }

        mControl = CreateWindowW(className, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                 mParent, (HMENU)mId, nullptr, this);
    }

    friend class PanelBase;

public:
    Panel(HWND parent, intptr_t id) : mParent(parent), mId(id) {}
};

class Window : public PanelBase {
    virtual bool IsTopLevel() override { return true; }

public:
    void Create() {
        const wchar_t *className = L"MyWindow";

        static bool registered = false;
        if (!registered) {
            RegisterWindowClass(className);
            registered = true;
        }

        SIZE size = InitialSize();
        RECT rect = {0, 0, size.cx, size.cy};
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, false, WS_EX_OVERLAPPEDWINDOW);

        POINT pos = InitialPos();
        CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, className, InitialTitle(),
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top,
                        nullptr, nullptr, nullptr, this);

        if (size.cx < 0 && mRootChild) {
            SetSize(mRootChild->GetIdealSize());
        }
    }

    void SetSize(SIZE size) {
        RECT sizeRect = {0, 0, size.cx, size.cy};
        AdjustWindowRectEx(&sizeRect, WS_OVERLAPPEDWINDOW, false, WS_EX_OVERLAPPEDWINDOW);

        RECT rect = GetPosition();
        rect.right = rect.left + sizeRect.right - sizeRect.left;
        rect.bottom = rect.top + sizeRect.bottom - sizeRect.top;
        SetPosition(rect);
    }

    virtual const wchar_t *InitialTitle() { return L""; }
    virtual POINT InitialPos() { return POINT{CW_USEDEFAULT, CW_USEDEFAULT}; }
    virtual SIZE InitialSize() { return SIZE{-1, -1}; }
};

class MainWindow : public Window {
    virtual void OnDestroy() override { PostQuitMessage(0); }
};

template <class THook>
void ProcessWindows(THook &&hook) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        hook(&msg);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ProcessWindows() {
    ProcessWindows([](MSG *) {});
}

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
