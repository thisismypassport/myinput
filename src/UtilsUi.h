#pragma once
#include "UtilsUiBase.h"
#include "resource.h"
#include <CommCtrl.h>
#include <Uxtheme.h>

RECT ToRect(SIZE size) { return RECT{0, 0, size.cx, size.cy}; }
RECT ToRect(POINT pt, SIZE size) { return RECT{pt.x, pt.y, pt.x + size.cx, pt.y + size.cy}; }
POINT ToPoint(SIZE size) { return POINT(size.cx, size.cy); }
LONG RectWidth(const RECT &rect) { return rect.right - rect.left; }
LONG RectHeight(const RECT &rect) { return rect.bottom - rect.top; }
LONG RectCenterX(const RECT &rect) { return (rect.right + rect.left) / 2; }
LONG RectCenterY(const RECT &rect) { return (rect.bottom + rect.top) / 2; }
POINT RectPoint(const RECT &rect) { return POINT(rect.left, rect.top); }
POINT RectCenter(RECT rect) { return POINT{RectCenterX(rect), RectCenterY(rect)}; }
SIZE RectSize(RECT rect) { return SIZE{RectWidth(rect), RectHeight(rect)}; }

constexpr int DefaultDpi = 96;
static int gDpi = DefaultDpi;

int DpiScale(int value) {
    return value > 0 && gDpi != DefaultDpi ? MulDiv(value, gDpi, DefaultDpi) : value;
}

SIZE DpiScale(SIZE value) {
    return value.cx > 0 && value.cy > 0 && gDpi != DefaultDpi ? SIZE{MulDiv(value.cx, gDpi, DefaultDpi), MulDiv(value.cy, gDpi, DefaultDpi)} : value;
}

class UiBase {
public:
    virtual ~UiBase() {}
    virtual void OnCommand(WORD value) {}
    virtual void OnNotify(NMHDR *header, LRESULT *ptrResult) {}
    virtual void OnDrawItem(DRAWITEMSTRUCT *info) {}
    virtual void OnIndirectCommand() {}

    void OnNew() {} // called via template
};

class ImageBase : public UiBase {
public:
    virtual int GetType() = 0;
    virtual HANDLE GetHandle() = 0;
    virtual SIZE GetSize() = 0;
    virtual HBITMAP GetBitmap() = 0;
};

class IconBase : public ImageBase {
protected:
    HICON mIcon = NULL;
    HBITMAP mBitmap = NULL;
    SIZE mSize;

    IconBase(HICON icon, SIZE size) : mIcon(icon), mSize(size) {}

public:
    ~IconBase() {
        DestroyIcon(mIcon);
        DeleteObject(mBitmap);
    }

    int GetType() override { return IMAGE_ICON; }
    HANDLE GetHandle() override { return mIcon; }
    SIZE GetSize() override { return mSize; }

    HBITMAP GetBitmap() override;
};

class StockIcon : public IconBase {
    static HICON GetIcon(SHSTOCKICONID id, bool large) {
        int flags = SHGSI_ICON | (large ? SHGSI_LARGEICON : SHGSI_SMALLICON);

        SHSTOCKICONINFO info;
        info.cbSize = sizeof(info);
        return SHGetStockIconInfo(id, flags, &info) == S_OK ? info.hIcon : nullptr;
    }

    SHSTOCKICONID mId;
    bool mLarge;

    template <bool large>
    static SIZE GetSize() {
        static int x = GetSystemMetrics(large ? SM_CXICON : SM_CXSMICON);
        static int y = GetSystemMetrics(large ? SM_CYICON : SM_CYSMICON);
        return SIZE{x, y};
    }

public:
    StockIcon(SHSTOCKICONID id, bool large = false) : IconBase(NULL, GetSize(large)), mId(id), mLarge(large) {}

    HANDLE GetHandle() override {
        if (!mIcon) {
            mIcon = GetIcon(mId, mLarge);
        }
        return mIcon;
    }

    static SIZE GetSize(bool large) {
        return large ? GetSize<true>() : GetSize<false>();
    }
};

class DrawCtxt : public UiBase {
    HDC mDC = NULL;
    HBITMAP mPrevBmp = NULL;
    HWND mWindow = NULL;
    bool mRelease = false;
    bool mDelete = false;

public:
    DrawCtxt(HDC dc) { mDC = dc; }
    DrawCtxt(HWND window) {
        mWindow = window;
        mDC = GetDC(window);
        mRelease = true;
    }
    DrawCtxt(SIZE size, HBITMAP *outBmp) {
        BITMAPINFO info = {{sizeof info.bmiHeader, size.cx, size.cy, 1, 32, BI_RGB}};

        HDC dc = GetDC(NULL);
        mDC = CreateCompatibleDC(dc);
        void *unused;
        *outBmp = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &unused, nullptr, 0);
        mPrevBmp = (HBITMAP)SelectObject(mDC, *outBmp);
        ReleaseDC(nullptr, dc);
        mDelete = true;
    }
    ~DrawCtxt() {
        if (mPrevBmp) {
            SelectObject(mDC, mPrevBmp);
        }

        if (mDelete) {
            DeleteDC(mDC);
        } else if (mRelease) {
            ReleaseDC(mWindow, mDC);
        }
    }

    void Draw(IconBase *icon, POINT pos) {
        SIZE size = icon->GetSize();
        DrawIconEx(mDC, pos.x, pos.y, (HICON)icon->GetHandle(), size.cx, size.cy, 0, nullptr, DI_NORMAL);
    }
    void Rectangle(RECT rect) {
        ::Rectangle(mDC, rect.left, rect.top, rect.right, rect.bottom);
    }
    void Ellipse(RECT rect) {
        ::Ellipse(mDC, rect.left, rect.top, rect.right, rect.bottom);
    }
    void Line(POINT start, POINT end) {
        POINT prev;
        MoveToEx(mDC, start.x, start.y, &prev);
        LineTo(mDC, end.x, end.y);
        MoveToEx(mDC, prev.x, prev.y, nullptr);
    }

    TEXTMETRICW TextMetrics() { return GetOutput(GetTextMetricsW, mDC); }
    RECT Measure(const wchar_t *text, int textSize = -1) {
        RECT rect = {};
        DrawTextW(mDC, text, textSize, &rect, DT_NOPREFIX | DT_CALCRECT);
        return rect;
    }

    class Object {
    protected:
        HGDIOBJ mObj;

    public:
        ~Object() { DeleteObject(mObj); }
        HGDIOBJ Get() const { return mObj; }
    };

    struct Pen : public Object {
        Pen(int width, COLORREF color) { mObj = CreatePen(PS_SOLID, DpiScale(width), color); }
    };

    class Set {
        HDC mDC;
        HGDIOBJ mPrev;

    public:
        Set(const DrawCtxt &draw, HGDIOBJ obj) : mDC(draw.mDC) { mPrev = SelectObject(mDC, obj); }
        Set(const DrawCtxt &draw, const Object &obj) : Set(draw, obj.Get()) {}
        ~Set() { SelectObject(mDC, mPrev); }
    };
};

HBITMAP IconBase::GetBitmap() {
    if (!mBitmap) {
        DrawCtxt draw(mSize, &mBitmap);
        draw.Draw(this, {});
    }
    return mBitmap;
}

class Control : public UiBase {
    Control *mParent = nullptr;
    bool mDisplayed = true;
    bool mVisible = true;
    bool mEnabled = true;
    bool mParentsVisible = false;
    bool mParentsEnabled = true;

protected:
    void SetRoot() { mParentsVisible = true; }

    enum class SizeType {
        Min,
        Ideal,
    };
    enum class RectType {
        Real,
        ForWrapping,
    };
    enum class HideType {
        Remove,
        Keep,
        FromParent,
    };

public:
    Control *GetParent() { return mParent; }
    void SetParent(Control *parent) {
        mParent = parent;
        OnShow(parent ? parent->IsVisibleFromRoot() : false, HideType::FromParent);
    }

    bool IsDisplayed() { return mDisplayed; }
    bool IsVisible() { return mVisible; }
    bool IsEnabled() { return mEnabled; }
    bool IsVisibleFromRoot() { return mParentsVisible && mVisible; }
    bool IsEnabledFromRoot() { return mParentsEnabled && mEnabled; }

    virtual Control *InitialFocus() { return nullptr; }

    virtual bool OnShow(bool show, HideType type) {
        bool old = IsVisibleFromRoot();
        if (type == HideType::FromParent) {
            mParentsVisible = show;
        } else {
            mVisible = show;
            bool newDisplayed = show || type == HideType::Keep;
            if (newDisplayed != mDisplayed) {
                mDisplayed = newDisplayed;
                if (mParent) {
                    mParent->OnChildResize();
                }
            }
        }
        return old != IsVisibleFromRoot();
    }
    virtual bool OnEnable(bool enable, HideType type) {
        bool old = IsEnabledFromRoot();
        if (type == HideType::FromParent) {
            mParentsEnabled = enable;
        } else {
            mEnabled = enable;
        }
        return old != IsEnabledFromRoot();
    }

    void Show(bool show = true) { OnShow(show, HideType::Keep); }
    void Hide() { Show(false); }
    void Display(bool show = true) { OnShow(show, HideType::Remove); }
    void Undisplay() { Show(false); }

    void Enable(bool enable = true) { OnEnable(enable, HideType::Keep); }
    void Disable() { Enable(false); }

    virtual SIZE GetSize(SizeType type) // should be cached!
    {
        if (type == SizeType::Min) {
            return DpiScale(SIZE(10, 10));
        } else {
            return DpiScale(SIZE(100, 100));
        }
    }

    virtual void OnSetRect(RECT rect, RectType type = RectType::Real) = 0;
    virtual void OnSetActive(bool active, void *root, void *prev) {}
    virtual void SetFocus() {}

    virtual void OnChildResize(RectType type = RectType::Real) {
        if (mParent) {
            mParent->OnChildResize(type);
        }
    }
    virtual bool OnKeyMsg(MSG *msg) { return mParent ? mParent->OnKeyMsg(msg) : false; }
};

class HwndBase : public Control {
protected:
    HWND mControl = nullptr;

    void InitFont() {
        SendMessageW(mControl, WM_SETFONT, (LPARAM)GetStockObject(DEFAULT_GUI_FONT), false);
    }

    SIZE DialogUnitsToSize(SIZE units) {
        DrawCtxt draw(mControl);
        DrawCtxt::Set setFont(draw, GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW metrics = draw.TextMetrics();

        LONG cx = MulDiv(metrics.tmAveCharWidth, units.cx, 4);
        LONG cy = MulDiv(metrics.tmHeight, units.cy, 8);
        return {cx, cy};
    }

    Path GetText(int *outLength = nullptr) {
        int size = GetWindowTextLengthW(mControl) + 1;
        if (outLength) {
            *outLength = size - 1;
        }

        Path text(size);
        GetWindowTextW(mControl, text, size);
        return text;
    }

    bool ChangeText(const wchar_t *newText) {
        Path oldText = GetText();
        if (tstreq(oldText, newText)) {
            return false;
        }

        SendMessageW(mControl, WM_SETTEXT, 0, (LPARAM)newText);
        return true;
    }

    SIZE GetWindowTextSize() {
        int length;
        Path text = GetText(&length);

        DrawCtxt draw(mControl);
        DrawCtxt::Set setFont(draw, GetStockObject(DEFAULT_GUI_FONT));
        RECT rect = draw.Measure(text, length);
        return RectSize(rect);
    }

    int GetId() { return GetWindowLongW(mControl, GWL_ID); }

    static HwndBase *GetUserData(HWND hwnd) {
        return (HwndBase *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    void SetUserData() {
        SetWindowLongPtrW(mControl, GWLP_USERDATA, (LONG_PTR)this);
    }

    static LRESULT CALLBACK SubclassWinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR ref) {
        return ((HwndBase *)ref)->OnSubclassMessage(hwnd, msg, wParam, lParam);
    }

    virtual LRESULT OnSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    void Subclass() {
        SetWindowSubclass(mControl, SubclassWinProc, 0, (DWORD_PTR)this);
    }

    void Post(function<void()> &&func) {
        PostMessageW(::GetParent(mControl), WM_APP, (WPARAM) new function<void()>(move(func)), true);
    }

    void TrackMouseLeave(bool on) {
        TRACKMOUSEEVENT ev = {};
        ev.cbSize = sizeof ev;
        ev.dwFlags = TME_LEAVE | (on ? 0 : TME_CANCEL);
        ev.hwndTrack = mControl;
        TrackMouseEvent(&ev);
    }

    friend class PopupMenu;
    friend class Shortcuts;

public:
    virtual ~HwndBase() {
        auto control = mControl;
        mControl = nullptr; // too late to be used from msg proc (no virtuals)
        DestroyWindow(control);
    }

    virtual void OnCtlColor(HDC dc) {}
    virtual void OnScroll(int op) {}

    void SetFocus() override { ::SetFocus(mControl); }

    bool OnShow(bool show, HideType type) override {
        bool changed = Control::OnShow(show, type);
        if (changed) {
            ShowWindow(mControl, show ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
        return changed;
    }

    bool OnEnable(bool enable, HideType type) override {
        bool changed = Control::OnEnable(enable, type);
        if (changed) {
            EnableWindow(mControl, enable);
        }
        return changed;
    }

    void OnSetRect(RECT rect, RectType type = RectType::Real) override {
        if (type == RectType::Real) {
            MoveWindow(mControl, rect.left, rect.top, RectWidth(rect), RectHeight(rect), false);
        }
    }

    POINT GetPopupPos() {
        RECT rect = GetOutput(GetWindowRect, mControl);
        return POINT{rect.left, rect.bottom};
    }

    friend class PreventRedraw;
};

class PreventRedraw {
    HWND mControl;
    bool mPrevent;

public:
    PreventRedraw(HwndBase *ctrl, bool prevent = true) : mControl(ctrl->mControl) {
        mPrevent = prevent && IsWindowVisible(mControl);
        if (mPrevent) {
            SendMessageW(mControl, WM_SETREDRAW, false, 0);
        }
    }

    ~PreventRedraw() {
        if (mPrevent) {
            SendMessageW(mControl, WM_SETREDRAW, true, 0);
            RedrawWindow(mControl, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
    }
};

template <typename TBase, typename TBuddy>
class BuddyBase : public TBase {
    using HideType = TBase::HideType;

protected:
    TBuddy mBuddy;

    bool OnShow(bool show, HideType type) override {
        bool changed = HwndBase::OnShow(show, type);
        if (changed) {
            mBuddy.OnShow(show, HideType::FromParent);
        }
        return changed;
    }

    bool OnEnable(bool enable, HideType type) override {
        bool changed = HwndBase::OnEnable(enable, type);
        if (changed) {
            mBuddy.OnEnable(enable, HideType::FromParent);
        }
        return changed;
    }

    void OnSetActive(bool active, void *root, void *prev) override {
        HwndBase::OnSetActive(active, root, prev);
        mBuddy.OnSetActive(active, root, prev);
    }

public:
    template <typename... TArgs>
    BuddyBase(TArgs... args) : mBuddy(forward<TArgs>(args)...) {
        mBuddy.SetParent(this);
    }
};

class Label : public HwndBase {
    SIZE mIdealSize = {-1, -1};

public:
    Label(HWND parent, intptr_t id, const wchar_t *text = L"") {
        mControl = CreateWindowW(WC_STATICW, text, WS_CHILD | SS_NOPREFIX,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    Label(HWND parent, intptr_t id, ImageBase *image) {
        int style = image->GetType() == IMAGE_ICON ? SS_ICON : SS_BITMAP;
        mIdealSize = image->GetSize();

        mControl = CreateWindowW(WC_STATICW, nullptr, WS_CHILD | SS_REALSIZECONTROL | style,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);

        SendMessageW(mControl, STM_SETIMAGE, image->GetType(), (LPARAM)image->GetHandle());
    }

    SIZE GetSize(SizeType type) override {
        if (mIdealSize.cx < 0) {
            mIdealSize = GetWindowTextSize();
        }
        return mIdealSize;
    }

    void SetText(const wchar_t *text) {
        if (ChangeText(text)) {
            mIdealSize = {-1, -1};
            OnChildResize();
        }
    }
};

class StyledLabel : public Label {
    COLORREF mColor;

public:
    StyledLabel(HWND parent, intptr_t id, const wchar_t *text = L"", COLORREF color = 0) : Label(parent, id, text) {
        mColor = color;
        SetUserData();
    }

    void SetColor(COLORREF color) {
        mColor = color;
        InvalidateRect(mControl, nullptr, false);
    }

    void OnCtlColor(HDC dc) override {
        SetTextColor(dc, mColor);
    }
};

class Separator : public HwndBase {
public:
    enum SepType {
        Horz = SS_ETCHEDHORZ,
        Vert = SS_ETCHEDVERT,
        Box = SS_ETCHEDFRAME,
    };

    Separator(HWND parent, intptr_t id, SepType type = SepType::Horz) : mType(type) {
        mControl = CreateWindowW(WC_STATICW, nullptr, WS_CHILD | (int)type,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
    }

    SIZE GetSize(SizeType type) override {
        SIZE size = {};
        if (mType == SepType::Horz) {
            size.cy = 2;
        } else if (mType == SepType::Vert) {
            size.cx = 2;
        }
        return size;
    }

private:
    SepType mType;
};

class DrawingBase : public HwndBase {
public:
    DrawingBase(HWND parent, intptr_t id) {
        mControl = CreateWindowW(WC_STATICW, nullptr, WS_CHILD | SS_OWNERDRAW,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
    }

    SIZE GetSize(SizeType type) override { return DpiScale(Size()); }

    void OnDrawItem(DRAWITEMSTRUCT *info) override {
        DrawCtxt ctxt(info->hDC);
        Draw(ctxt, info->rcItem);
    }

    void Invalidate() { InvalidateRect(mControl, nullptr, false); }

    virtual SIZE Size() = 0;
    virtual void Draw(DrawCtxt &draw, RECT rect) = 0;
};

class ButtonBase : public HwndBase {
    ImageBase *mImage;

protected:
    SIZE mIdealSize = {-1, -1};

    ButtonBase(HWND parent, intptr_t id, const wchar_t *text, ImageBase *image, int styles) : mImage(image) {
        mControl = CreateWindowW(WC_BUTTONW, text, WS_CHILD | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();

        if (image) {
            SendMessageW(mControl, BM_SETIMAGE, image->GetType(), (LPARAM)image->GetHandle());
        }
    }

public:
    SIZE GetSize(SizeType type) override {
        if (mIdealSize.cx < 0) {
            SIZE size = {};
            SendMessageW(mControl, BCM_GETIDEALSIZE, 0, (LPARAM)&size);
            mIdealSize = size;
        }
        return mIdealSize;
    }

    ImageBase *GetImage() { return mImage; }

    void SetText(const wchar_t *text) {
        if (ChangeText(text)) {
            mIdealSize = {-1, -1};
            OnChildResize();
        }
    }
};

class ActionButtonBase : public ButtonBase {
    function<void()> mAction;

protected:
    ActionButtonBase(HWND parent, intptr_t id, const wchar_t *text, ImageBase *image, function<void()> &&action, int styles) : ButtonBase(parent, id, text, image, styles), mAction(move(action)) {}

public:
    void Click() {
        if (mAction) {
            mAction();
        }
    }

    void OnCommand(WORD value) override {
        if (value == BN_CLICKED && mAction) {
            mAction();
        }
    }
    void OnIndirectCommand() override { Click(); }
};

class Button : public ActionButtonBase {
public:
    Button(HWND parent, intptr_t id, const wchar_t *text, ImageBase *image, function<void()> &&action) : ActionButtonBase(parent, id, text, image, move(action), WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE) {}

    Button(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action) : Button(parent, id, text, nullptr, move(action)) {}
};

class CheckBase : public ButtonBase {
    function<void(bool value)> mAction;

protected:
    CheckBase(HWND parent, intptr_t id, const wchar_t *text, bool value, function<void(bool)> &&action, int styles) : ButtonBase(parent, id, text, nullptr, WS_TABSTOP | styles), mAction(move(action)) {
        if (value) {
            Set();
        }
    }

public:
    bool Get() { return SendMessageW(mControl, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void Set(bool value = true) { SendMessageW(mControl, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
    void Clear() { Set(false); }

    void Click() {
        bool value = !Get();
        Set(value);
        if (mAction) {
            mAction(value);
        }
    }
    void Click(bool value) {
        if (value != Get()) {
            Click();
        }
    }

    void OnCommand(WORD value) override {
        if (value == BN_CLICKED && mAction) {
            mAction(Get());
        }
    }

    void OnIndirectCommand() override { Click(); }
};

template <typename T>
concept IsBool = std::same_as<T, bool>;

class CheckBox : public CheckBase {
public:
    CheckBox(HWND parent, intptr_t id, const wchar_t *text, IsBool auto value, function<void(bool)> &&action = {}) : CheckBase(parent, id, text, value, move(action), BS_AUTOCHECKBOX | BS_MULTILINE) {}
    CheckBox(HWND parent, intptr_t id, const wchar_t *text, function<void(bool)> &&action = {}) : CheckBox(parent, id, text, false, move(action)) {}
};

class CheckButton : public CheckBase {
public:
    CheckButton(HWND parent, intptr_t id, const wchar_t *text, IsBool auto value, function<void(bool)> &&action = {}) : CheckBase(parent, id, text, value, move(action), BS_AUTOCHECKBOX | BS_MULTILINE | BS_PUSHLIKE) {}
    CheckButton(HWND parent, intptr_t id, const wchar_t *text, function<void(bool)> &&action = {}) : CheckButton(parent, id, text, false, move(action)) {}
};

enum class MaybeBool {
    False,
    True,
    Partial,
};

MaybeBool ToggleUp(MaybeBool v) { return v == MaybeBool::Partial ? MaybeBool::False : (MaybeBool)((int)v + 1); }
MaybeBool ToggleDown(MaybeBool v) { return v == MaybeBool::False ? MaybeBool::Partial : (MaybeBool)((int)v - 1); }

class PartialCheckBox : public ButtonBase {
    function<void(MaybeBool value)> mAction;
    function<MaybeBool(MaybeBool)> mToggle;

public:
    PartialCheckBox(HWND parent, intptr_t id, const wchar_t *text, MaybeBool value,
                    function<MaybeBool(MaybeBool)> &&toggle, function<void(MaybeBool)> &&action = {}) : ButtonBase(parent, id, text, nullptr, WS_TABSTOP | BS_3STATE), mToggle(move(toggle)), mAction(move(action)) {
        if (value != MaybeBool::False) {
            Set(value);
        }
    }

    PartialCheckBox(HWND parent, intptr_t id, const wchar_t *text,
                    function<MaybeBool(MaybeBool)> &&toggle, function<void(MaybeBool)> &&action = {}) : PartialCheckBox(parent, id, text, MaybeBool::False, move(toggle), move(action)) {}

    MaybeBool Get() {
        LPARAM state = SendMessageW(mControl, BM_GETCHECK, 0, 0);
        return state == BST_CHECKED ? MaybeBool::True : state == BST_INDETERMINATE ? MaybeBool::Partial
                                                                                   : MaybeBool::False;
    }

    void Set(MaybeBool value) {
        int state = value == MaybeBool::True ? BST_CHECKED : value == MaybeBool::False ? BST_UNCHECKED
                                                                                       : BST_INDETERMINATE;
        SendMessageW(mControl, BM_SETCHECK, state, 0);
    }
    void Clear() { Set(MaybeBool::False); }

    void OnCommand(WORD value) override {
        if (value == BN_CLICKED) {
            MaybeBool value = mToggle(Get());
            Set(value);
            if (mAction) {
                mAction(value);
            }
        }
    }

    SIZE GetSize(SizeType type) override // sigh...
    {
        if (mIdealSize.cx < 0) {
            mIdealSize = GetWindowTextSize();
            mIdealSize.cx += DialogUnitsToSize({0, 11}).cy;
            mIdealSize.cy = max(mIdealSize.cy, DialogUnitsToSize({0, 9}).cy);
        }
        return mIdealSize;
    }
};

class RadioBase;

class RadioGroupBase : public UiBase {
    vector<RadioBase *> mChildren;

protected:
    using UiBase::UiBase;

    friend class RadioBase;

    int Add(RadioBase *child) {
        int idx = (int)mChildren.size();
        mChildren.push_back(child);
        return idx;
    }

public:
    const vector<RadioBase *> &GetChildren() { return mChildren; }
};

template <class TValue>
class RadioGroup : public RadioGroupBase {
    function<void(const TValue &)> mAction;

protected:
    void OnClicked(const TValue &value) {
        if (mAction) {
            mAction(value);
        }
    }

    friend class RadioBase;

public:
    RadioGroup(HWND, intptr_t, function<void(const TValue &)> &&action = {}) : mAction(move(action)) {}
};

class RadioBase : public ActionButtonBase {
    RadioGroupBase *mGroup = nullptr;

protected:
    RadioBase(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action, int styles, int index = -1) : ActionButtonBase(parent, id, text, nullptr, move(action), BS_AUTORADIOBUTTON | BS_MULTILINE | styles | (index == 0 ? (WS_TABSTOP | WS_GROUP) : 0)) {}

    template <class TValue>
    RadioBase(HWND parent, intptr_t id, const wchar_t *text, RadioGroup<TValue> *group, const TValue &value, int styles) : RadioBase(
                                                                                                                               parent, id, text, [=] { group->OnClicked(value); }, styles, group->Add(this)) { mGroup = group; }

public:
    bool Get() { return SendMessageW(mControl, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void Set(bool value = true) { SendMessageW(mControl, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
    void Clear() { Set(false); }

    void Click() {
        Set();
        if (mGroup) {
            for (auto child : mGroup->GetChildren()) {
                if (child != this) {
                    child->Clear();
                }
            }
        }
        ActionButtonBase::Click();
    }

    void OnIndirectCommand() override { Click(); }
};

class RadioBox : public RadioBase {
public:
    RadioBox(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action = {}) : RadioBase(parent, id, text, move(action), 0) {}

    template <class TValue>
    RadioBox(HWND parent, intptr_t id, const wchar_t *text, RadioGroup<TValue> *group, const TValue &value) : RadioBase(parent, id, text, group, value, 0) {}
};

class RadioButton : public RadioBase {
public:
    RadioButton(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action = {}) : RadioBase(parent, id, text, move(action), BS_PUSHLIKE) {}

    template <class TValue>
    RadioButton(HWND parent, intptr_t id, const wchar_t *text, RadioGroup<TValue> *group, const TValue &value) : RadioBase(parent, id, text, group, value, BS_PUSHLIKE) {}
};

struct PopupIntf {
    virtual void Create(POINT pos) = 0;
};

class SplitButton : public ActionButtonBase {
    PopupIntf *mPopup = nullptr;

public:
    SplitButton(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&action) : ActionButtonBase(parent, id, text, nullptr, move(action), WS_TABSTOP | BS_SPLITBUTTON | BS_MULTILINE) {}

    SplitButton(HWND parent, intptr_t id, const wchar_t *text, PopupIntf *popup, function<void()> &&action) : SplitButton(parent, id, text, move(action)) { SetPopup(popup); }

    void SetPopup(PopupIntf *popup) { mPopup = popup; }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == BCN_DROPDOWN && mPopup) {
            auto info = (NMBCDROPDOWN *)header;
            RECT selfRect = GetOutput(GetWindowRect, mControl);
            mPopup->Create(POINT{selfRect.left + info->rcButton.left, selfRect.top + info->rcButton.bottom});
        }
    }
};

class EditBase : public HwndBase {
    function<void()> mChanged;
    function<void()> mFinished;
    bool mHasChanges = false;
    bool mProgrammatic = false;

    friend class EditNumberBase;

protected:
    EditBase(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&changed, int styles) : mChanged(move(changed)) {
        mControl = CreateWindowW(WC_EDITW, text,
                                 WS_TABSTOP | WS_CHILD | WS_BORDER | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

public:
    Path Get() { return GetText(); }

    void Set(const wchar_t *value) {
        mProgrammatic = true;
        SetWindowTextW(mControl, value);
        mProgrammatic = false;
    }

    void SetOnFinish(function<void()> &&finished) { mFinished = move(finished); }

    void SetReadOnly(bool readOnly) {
        SendMessageW(mControl, EM_SETREADONLY, readOnly, 0);
    }

    void OnCommand(WORD value) override {
        if (value == EN_CHANGE && !mProgrammatic) {
            mHasChanges = true;
            if (mChanged) {
                mChanged();
            }
        } else if (value == EN_KILLFOCUS && mHasChanges) {
            mHasChanges = false;
            if (mFinished) {
                mFinished();
            }
        }
    }
};

class EditLine : public EditBase {
    LONG mIdealHeight = -1;

public:
    EditLine(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&changed = {}) : EditBase(parent, id, text, move(changed), ES_AUTOHSCROLL | ES_LEFT | ES_NOHIDESEL) {}

    EditLine(HWND parent, intptr_t id, function<void()> &&changed = {}) : EditLine(parent, id, nullptr, move(changed)) {}

    SIZE GetSize(SizeType type) override {
        if (mIdealHeight < 0) {
            mIdealHeight = DialogUnitsToSize({0, 11}).cy;
        }
        return SIZE{Control::GetSize(type).cx, mIdealHeight};
    }
};

class EditBox : public EditBase {
public:
    EditBox(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&changed = {}) : EditBase(parent, id, text, move(changed), ES_AUTOVSCROLL | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL) {}

    EditBox(HWND parent, intptr_t id, function<void()> &&changed = {}) : EditBox(parent, id, nullptr, move(changed)) {}
};

class EditNumberBase : public BuddyBase<HwndBase, EditLine> {
    int mUpDownWidth = -1;

    int GetUpDownWidth() {
        if (mUpDownWidth < 0) {
            mUpDownWidth = DialogUnitsToSize({11, 0}).cx;
        }
        return mUpDownWidth;
    }

    virtual void OnChange() = 0;

protected:
    EditNumberBase(HWND parent, intptr_t id, int styles) : BuddyBase(parent, id, [this] { OnChange(); }) {
        mControl = CreateWindowW(UPDOWN_CLASSW, nullptr,
                                 WS_TABSTOP | WS_CHILD |
                                     UDS_AUTOBUDDY | UDS_ARROWKEYS | styles,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    SIZE GetSize(SizeType type) override {
        SIZE size = mBuddy.GetSize(type);
        size.cx += GetUpDownWidth();
        return size;
    }

    void OnSetRect(RECT rect, RectType type) override {
        rect.right -= GetUpDownWidth();
        mBuddy.OnSetRect(rect, type);

        rect.left = rect.right;
        rect.right += GetUpDownWidth();
        HwndBase::OnSetRect(rect, type);
    }

    void OnCommand(WORD value) override { mBuddy.OnCommand(value); }
    void OnNotify(NMHDR *header, LRESULT *ptrResult) override { mBuddy.OnNotify(header, ptrResult); }

public:
    void SetRange(int min, int max) { SendMessageW(mControl, UDM_SETRANGE32, min, max); }
    int Get() { return (int)SendMessageW(mControl, UDM_GETPOS32, 0, 0); }

    void Set(int value) {
        mBuddy.mProgrammatic = true;
        SendMessageW(mControl, UDM_SETPOS32, 0, value);
        mBuddy.mProgrammatic = false;
    }
};

template <typename TInt>
class EditInt : public EditNumberBase {
    static_assert(std::is_integral_v<TInt> && std::is_signed_v<TInt> && sizeof(TInt) <= sizeof(int));

    function<void(TInt)> mChanged;

    void OnChange() override {
        if (mChanged) {
            mChanged(Get());
        }
    }

public:
    EditInt(HWND parent, intptr_t id, function<void(TInt)> &&changed = {}) : EditNumberBase(parent, id, UDS_SETBUDDYINT), mChanged(move(changed)) {
        SetRange(numeric_limits<TInt>::min(), numeric_limits<TInt>::max());
    }

    EditInt(HWND parent, intptr_t id, TInt value, function<void(TInt)> &&changed = {}) : EditInt(parent, id, move(changed)) { Set(value); }

    void SetRange(TInt min, TInt max) { EditNumberBase::SetRange(min, max); }
    void Set(TInt value) { EditNumberBase::Set(value); }
    TInt Get() { return (TInt)EditNumberBase::Get(); }
    void Add(TInt value) { Set(Get() + value); }
};

template <typename TFloat>
class EditFloat : public EditNumberBase {
    static_assert(std::is_floating_point_v<TFloat>);

    function<void(TFloat)> mChanged;
    TFloat mMin = 0, mMax = 0, mStep = 0;
    int mPrecision = -1;

    void OnChange() override {
        if (mChanged) {
            mChanged(Get());
        }
    }

public:
    EditFloat(HWND parent, intptr_t id, function<void(TFloat)> &&changed = {}) : EditNumberBase(parent, id, 0), mChanged(move(changed)) {
        EditNumberBase::SetRange(0, 100);
        EditNumberBase::Set(50);
        SetRange(numeric_limits<TFloat>::lowest(), numeric_limits<TFloat>::max(), 1);
    }

    EditFloat(HWND parent, intptr_t id, TFloat value, function<void(TFloat)> &&changed = {}) : EditFloat(parent, id, move(changed)) { Set(value); }

    void SetRange(TFloat min, TFloat max, TFloat step) {
        mMin = min, mMax = max, mStep = step;
    }

    void SetPrecision(int precision) { mPrecision = precision; }

    void Set(TFloat value) {
        value = Clamp(value, mMin, mMax);
        if (mPrecision >= 0) {
            mBuddy.Set(StrFromValue<wchar_t>(value, std::chars_format::fixed, mPrecision).c_str());
        } else {
            mBuddy.Set(StrFromValue<wchar_t>(value, std::chars_format::general, std::numeric_limits<TFloat>::digits10).c_str());
        }
    }

    TFloat Get() {
        Path text = mBuddy.Get();
        TFloat value;
        if (!StrToValue(text, &value)) {
            return mMin;
        }

        return Clamp(value, mMin, mMax);
    }

    void Add(TFloat value) { Set(Get() + value); }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == UDN_DELTAPOS) {
            auto info = ((NMUPDOWN *)header);
            Set(Get() + info->iDelta * mStep);
            if (info->iPos < 25 || info->iPos > 75) {
                EditNumberBase::Set(50);
            }
            OnChange();
        }
    }
};

class UpDownButtons : public HwndBase {
    SIZE mIdealSize = {-1, -1};
    function<void(bool)> mAction;

    SIZE GetSize(SizeType type) override {
        if (mIdealSize.cx < 0) {
            mIdealSize = DialogUnitsToSize({11, 11});
        }
        return mIdealSize;
    }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == UDN_DELTAPOS) {
            auto info = ((NMUPDOWN *)header);
            mAction(info->iDelta < 0);
            if (info->iPos < 25 || info->iPos > 75) {
                SendMessageW(mControl, UDM_SETPOS32, 0, 50);
            }
        }
    }

public:
    UpDownButtons(HWND parent, intptr_t id, function<void(bool down)> &&action) : mAction(action) {
        mControl = CreateWindowW(UPDOWN_CLASSW, nullptr,
                                 WS_TABSTOP | WS_CHILD | UDS_ARROWKEYS,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
        SendMessageW(mControl, UDM_SETRANGE32, 0, 100);
        SendMessageW(mControl, UDM_SETPOS32, 0, 50);
    }
};

class Slider : public HwndBase {
public:
    enum Type {
        Horz = TBS_HORZ,
        Vert = TBS_VERT,
    };

    Slider(HWND parent, intptr_t id, int range, function<void(int)> &&changed = {}, Type type = Slider::Horz) : mChanged(move(changed)), mType(type) {
        mControl = CreateWindowW(TRACKBAR_CLASSW, nullptr,
                                 WS_TABSTOP | WS_CHILD | type | TBS_NOTICKS,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        SetRange(range);
        SetUserData();
    }

    void SetOnFinish(function<void(int)> &&finished) { mFinished = move(finished); }

    void SetRange(int range) { SetRange(0, range); }
    void SetRange(int min, int max) {
        SendMessageW(mControl, TBM_SETRANGEMIN, false, min);
        SendMessageW(mControl, TBM_SETRANGEMAX, true, max);
    }

    int Get() { return (int)SendMessageW(mControl, TBM_GETPOS, 0, 0); }
    void Set(int pos) { SendMessageW(mControl, TBM_SETPOS, true, pos); }

    void OnScroll(int op) override {
        if (op == TB_ENDTRACK) {
            return;
        }
        int pos = Get();
        if (mChanged) {
            mChanged(pos);
        }
        if (op != SB_THUMBTRACK && mFinished) {
            mFinished(pos);
        }
    }

private:
    Type mType;
    int mIdealSize = -1;
    function<void(int)> mChanged;
    function<void(int)> mFinished;

    SIZE GetSize(SizeType type) override {
        if (mType == Horz) {
            if (mIdealSize < 0) {
                mIdealSize = DialogUnitsToSize({0, 11}).cy;
            }
            return SIZE{Control::GetSize(type).cx, mIdealSize};
        } else {
            if (mIdealSize < 0) {
                mIdealSize = DialogUnitsToSize({0, 11}).cx;
            }
            return SIZE{mIdealSize, Control::GetSize(type).cx};
        }
    }
};

class ListBoxBase : public HwndBase {
protected:
    ListBoxBase(HWND parent, intptr_t id, int style) {
        mControl = CreateWindowW(WC_LISTBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_BORDER |
                                     LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | style,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

public:
    void *GetData(int idx) { return (void *)SendMessageW(mControl, LB_GETITEMDATA, idx, 0); }
    void SetData(int idx, void *data) { SendMessageW(mControl, LB_SETITEMDATA, idx, (LPARAM)data); }

    int Add(const wchar_t *str, void *data = nullptr) { return Insert(-1, str, data); }
    int Insert(int idx, const wchar_t *str, void *data = nullptr) {
        idx = (int)SendMessageW(mControl, LB_INSERTSTRING, idx, (LPARAM)str);
        if (data) {
            SetData(idx, data);
        }
        return idx;
    }

    void Remove(int idx) { SendMessageW(mControl, LB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, LB_RESETCONTENT, 0, 0); }
    int Count() { return (int)SendMessageW(mControl, LB_GETCOUNT, 0, 0); }

    int Find(const wchar_t *str) { return (int)SendMessageW(mControl, LB_FINDSTRINGEXACT, -1, (LPARAM)str); }
    Path Get(int idx) {
        int len = (int)SendMessageW(mControl, LB_GETTEXTLEN, idx, 0);
        if (len < 0) {
            return Path();
        }

        Path path(len + 1);
        SendMessageW(mControl, LB_GETTEXT, idx, (LPARAM)path.Get());
        return path;
    }
};

class ListBox : public ListBoxBase {
    function<void(int)> mAction;

public:
    ListBox(HWND parent, intptr_t id, function<void(int idx)> &&action = {}) : ListBoxBase(parent, id, 0), mAction(move(action)) {}

    int GetSelected() { return (int)SendMessageW(mControl, LB_GETCURSEL, 0, 0); }

    void SetSelected(int idx, bool action = true) {
        SendMessageW(mControl, LB_SETCURSEL, idx, 0);
        if (action && mAction) {
            mAction(idx);
        }
    }

    void OnCommand(WORD value) override {
        if (value == LBN_SELCHANGE && mAction) {
            mAction(GetSelected());
        }
    }
};

class MultiListBox : public ListBoxBase {
    function<void(const vector<int> &)> mAction;

public:
    MultiListBox(HWND parent, intptr_t id, function<void(const vector<int> &indices)> &&action = {}) : ListBoxBase(parent, id, LBS_EXTENDEDSEL), mAction(move(action)) {}

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

        if (action && mAction) {
            mAction(indices);
        }
    }

    void OnCommand(WORD value) override {
        if (value == LBN_SELCHANGE && mAction) {
            mAction(GetSelected());
        }
    }
};

class ComboBase : public HwndBase {
    LONG mIdealHeight = -1;
    bool mSelChanged = false;
    bool mIsOpen = false;
    function<void(bool)> mOpen;

protected:
    ComboBase(HWND parent, intptr_t id, int style) {
        mControl = CreateWindowW(WC_COMBOBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | CBS_HASSTRINGS | style,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
        SendMessageW(mControl, CB_SETEXTENDEDUI, true, 0); // to disable wheel
    }

    virtual void OnSelect() = 0;

    void OnOpen(bool value) {
        mIsOpen = value;
        if (mOpen) {
            mOpen(value);
        }
    }

public:
    void *GetData(int idx) { return (void *)SendMessageW(mControl, CB_GETITEMDATA, idx, 0); }
    void SetData(int idx, void *data) { SendMessageW(mControl, CB_SETITEMDATA, idx, (LPARAM)data); }

    void SetOnOpen(function<void(bool)> &&open) { mOpen = move(open); }

    int Add(const wchar_t *str, void *data = nullptr) { return Insert(-1, str, data); }
    int Insert(int idx, const wchar_t *str, void *data = nullptr) {
        idx = (int)SendMessageW(mControl, CB_INSERTSTRING, idx, (LPARAM)str);
        if (data) {
            SetData(idx, data);
        }
        return idx;
    }

    void Remove(int idx) { SendMessageW(mControl, CB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, CB_RESETCONTENT, 0, 0); }
    int Count() { return (int)SendMessageW(mControl, CB_GETCOUNT, 0, 0); }

    int Find(const wchar_t *str) { return (int)SendMessageW(mControl, CB_FINDSTRINGEXACT, -1, (LPARAM)str); }
    Path Get(int idx) {
        int len = (int)SendMessageW(mControl, CB_GETLBTEXTLEN, idx, 0);
        if (len < 0) {
            return Path();
        }

        Path path(len + 1);
        SendMessageW(mControl, CB_GETLBTEXT, idx, (LPARAM)path.Get());
        return path;
    }

    int FindByData(void *data) {
        for (int i = 0, count = Count(); i < count; i++) {
            if (GetData(i) == data) {
                return i;
            }
        }
        return -1;
    }

    int GetSelected() { return (int)SendMessageW(mControl, CB_GETCURSEL, 0, 0); }
    void SetSelected(int idx) { SendMessageW(mControl, CB_SETCURSEL, idx, 0); }

    Path GetSelectedStr() { return Get(GetSelected()); }
    void SetSelectedStr(const wchar_t *str) { SetSelected(Find(str)); }

    SIZE GetSize(SizeType type) override {
        if (mIdealHeight < 0) {
            mIdealHeight = DialogUnitsToSize({0, 11}).cy;
        }
        return SIZE{Control::GetSize(type).cx, mIdealHeight};
    }

    void OnCommand(WORD value) override {
        if (value == CBN_DROPDOWN) {
            OnOpen(true);
        } else if (value == CBN_CLOSEUP) {
            OnOpen(false);
            if (mSelChanged) {
                OnSelect();
                mSelChanged = false;
            }
        } else if (value == CBN_SELCHANGE) {
            if (mIsOpen) {
                mSelChanged = true;
            } else {
                OnSelect();
            }
        }
    }
};

class DropDownList : public ComboBase {
    function<void(int)> mAction;

    void OnSelect() override {
        if (mAction) {
            mAction(GetSelected());
        }
    }

public:
    DropDownList(HWND parent, intptr_t id, function<void(int idx)> &&action = {}) : ComboBase(parent, id, CBS_DROPDOWNLIST), mAction(move(action)) {}

    void SetSelected(int idx, bool action = true) {
        ComboBase::SetSelected(idx);
        if (action && mAction) {
            mAction(idx);
        }
    }

    void SetSelectedStr(const wchar_t *str, bool action = true) {
        SetSelected(Find(str), action);
    }
};

class DropDownPopup : public ComboBase {
    PopupIntf *mPopup = nullptr;
    HwndBase *mPopupBase = nullptr;
    bool mInPopup = false;

    void OnSelect() override {}

    virtual LRESULT OnSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            if (msg != WM_LBUTTONUP) {
                Popup();
            }
            return 0;
        }
        return HwndBase::OnSubclassMessage(hwnd, msg, wParam, lParam);
    }

    void Popup() {
        if (mInPopup) {
            return;
        }

        mInPopup = true;
        if (mPopup) {
            auto pos = (mPopupBase ? mPopupBase : this)->GetPopupPos();
            mPopup->Create(pos);
        }
        mInPopup = false;
    }

public:
    DropDownPopup(HWND parent, intptr_t id) : ComboBase(parent, id, CBS_DROPDOWNLIST) { Subclass(); }

    DropDownPopup(HWND parent, intptr_t id, PopupIntf *popup) : DropDownPopup(parent, id) { SetPopup(popup); }

    void SetPopup(PopupIntf *popup) { mPopup = popup; }
    void SetPopupBase(HwndBase *base) { mPopupBase = base; }

    void SetSelected(const wchar_t *str) {
        Clear();
        Add(str);
        ComboBase::SetSelected(0);
    }

    Path GetSelected() { return ComboBase::Get(0); }
    void *GetSelectedData() { return ComboBase::GetData(0); }

    void OnCommand(WORD value) override {
        if (value == CBN_DROPDOWN) // just in case reached
        {
            Popup();
            Post([this] {
                SendMessageW(mControl, CB_SHOWDROPDOWN, false, 0);
            });
        }
    }
};

class DropDownEditLine : public ComboBase {
    function<void()> mChanged;
    function<void()> mFinished;
    bool mHasChanges = false;
    bool mProgrammatic = false;

    void OnSelect() override {
        // text's not updated yet...
        if (mChanged || mFinished) {
            Post([this]() {
                mHasChanges = false;
                if (mChanged) {
                    mChanged();
                }
                if (mFinished) {
                    mFinished();
                }
            });
        }
    }

public:
    DropDownEditLine(HWND parent, intptr_t id, function<void()> &&changed = {}) : ComboBase(parent, id, CBS_DROPDOWN), mChanged(move(changed)) {}

    DropDownEditLine(HWND parent, intptr_t id, const wchar_t *text, function<void()> &&changed = {}) : DropDownEditLine(parent, id, move(changed)) {
        Set(text);
    }

    void SetOnFinish(function<void()> &&finished) { mFinished = move(finished); }

    Path Get() { return GetText(); }
    void Set(const wchar_t *value) {
        mProgrammatic = true;
        SetWindowTextW(mControl, value);
        mProgrammatic = false;
        mHasChanges = false;
    }

    void OnCommand(WORD value) override {
        if (value == CBN_EDITCHANGE && !mProgrammatic) {
            mHasChanges = true;
            if (mChanged) {
                mChanged();
            }
        } else if (value == CBN_KILLFOCUS && mHasChanges) {
            mHasChanges = false;
            if (mFinished) {
                mFinished();
            }
        }
        ComboBase::OnCommand(value);
    }
};

enum class ClickType {
    Left,
    Double,
    Right,
    RightDouble,
};

class ListViewBase : public HwndBase {
    function<void(int, bool)> mCheckAction;
    function<void(int, int, bool, bool)> mClickAction;
    function<bool(int)> mKeyAction;
    function<void()> mRectChangedAction;
    int mResizableColumn = -1;

    int StateToChecked(int state) { return ((state >> 12) & 3) - 1; }
    int CheckedToState(bool checked) { return (checked ? 2 : 1) << 12; }

    void DoClick(NMHDR *header, bool right, bool dblclk) {
        auto info = (NMITEMACTIVATE *)header;
        if (mClickAction) {
            mClickAction(info->iItem, info->iSubItem, right, dblclk);
        }
    }
    bool DoKey(int vk) {
        return mKeyAction ? mKeyAction(vk) : false;
    }

protected:
    bool mProgrammatic = false;
    bool mActionSet = false;
    bool mActionPosted = false;

    virtual void DoAction() = 0;

    void OnSetRect(RECT rect, RectType type) override {
        HwndBase::OnSetRect(rect, type);

        if (mResizableColumn >= 0) {
            int colIdx = 0;
            int totalWidth = 0;
            while (true) {
                if (colIdx != mResizableColumn) {
                    LVCOLUMNW column;
                    column.mask = LVCF_WIDTH;
                    if (!SendMessageW(mControl, LVM_GETCOLUMN, colIdx, (LPARAM)&column)) {
                        break;
                    }

                    totalWidth += column.cx;
                }
                colIdx++;
            }

            int resizableWidth = max<int>(RectWidth(rect) - totalWidth, 10);
            SendMessageW(mControl, LVM_SETCOLUMNWIDTH, mResizableColumn, resizableWidth);
        }
    }

    ListViewBase(HWND parent, intptr_t id, int style) {
        mControl = CreateWindowW(WC_LISTVIEWW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_BORDER |
                                     LVS_SHOWSELALWAYS | LVS_REPORT | LVS_NOSORTHEADER | style,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        SendMessageW(mControl, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        InitFont();
    }

public:
    void SetOnCheck(function<void(int idx, bool state)> &&checkAction = {}) {
        mCheckAction = move(checkAction);
        SendMessageW(mControl, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
    }
    void SetOnClick(function<void(int idx, int colIdx, bool right, bool dblclk)> &&action) {
        mClickAction = move(action);
    }
    void SetOnKey(function<bool(int key)> &&action) {
        mKeyAction = move(action);
    }
    void SetOnRectChanged(function<void()> &&action) {
        mRectChangedAction = move(action);
    }

    int InsertColumn(int idx, const wchar_t *str, int width = -1) {
        LVCOLUMNW item;
        item.mask = LVCF_TEXT | (width >= 0 ? LVCF_WIDTH : 0);
        item.pszText = (wchar_t *)str;
        item.cx = DpiScale(width);
        idx = (int)SendMessageW(mControl, LVM_INSERTCOLUMNW, idx, (LPARAM)&item);

        if (width < 0) {
            SetResizableColumn(idx);
        }

        return idx;
    }
    int AddColumn(const wchar_t *str, int width = -1) { return InsertColumn(INT_MAX, str, width); }

    void SetResizableColumn(int idx) {
        mResizableColumn = idx;
    }
    void CreateSingleColumn() {
        SetWindowLongW(mControl, GWL_STYLE, GetWindowLongW(mControl, GWL_STYLE) | LVS_NOCOLUMNHEADER);
        AddColumn(L"", 0);
        SetResizableColumn(0);
    }

    void *GetData(int idx) {
        LVITEMW item;
        item.mask = LVIF_PARAM;
        item.iItem = idx;
        item.iSubItem = 0;
        return SendMessageW(mControl, LVM_GETITEM, 0, (LPARAM)&item) ? (void *)item.lParam : nullptr;
    }
    void SetData(int idx, void *data) {
        LVITEMW item;
        item.mask = LVIF_PARAM;
        item.iItem = idx;
        item.iSubItem = 0;
        item.lParam = (LPARAM)data;
        SendMessageW(mControl, LVM_SETITEM, 0, (LPARAM)&item);
    }

    int Add(const wchar_t *str, void *data = nullptr) { return Insert(INT_MAX, str, data); }
    int Insert(int idx, const wchar_t *str, void *data = nullptr) {
        LVITEMW item;
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = (wchar_t *)str;
        item.lParam = (LPARAM)data;
        return (int)SendMessageW(mControl, LVM_INSERTITEMW, 0, (LPARAM)&item);
    }

    void Set(int idx, int colIdx, const wchar_t *str) {
        LVITEMW item;
        item.iSubItem = colIdx;
        item.pszText = (wchar_t *)str;
        SendMessageW(mControl, LVM_SETITEMTEXTW, idx, (LPARAM)&item);
    }
    void Set(int idx, const wchar_t *str) { Set(idx, 0, str); }

    void Remove(int idx) { SendMessageW(mControl, LVM_DELETEITEM, idx, 0); }
    void Clear() { SendMessageW(mControl, LVM_DELETEALLITEMS, 0, 0); }
    int Count() { return (int)SendMessageW(mControl, LVM_GETITEMCOUNT, 0, 0); }

    bool GetChecked(int idx) {
        int state = (int)SendMessageW(mControl, LVM_GETITEMSTATE, idx, LVIS_STATEIMAGEMASK);
        return StateToChecked(state) == 1;
    }
    void SetChecked(int idx, bool checked = true, bool action = true) {
        mProgrammatic = true;
        LVITEMW item;
        item.stateMask = LVIS_STATEIMAGEMASK;
        item.state = CheckedToState(checked);
        SendMessageW(mControl, LVM_SETITEMSTATE, idx, (LPARAM)&item);
        mProgrammatic = false;

        if (action && mCheckAction) {
            mCheckAction(idx, checked);
        }
    }

    RECT GetRect(int idx, int colIdx) {
        RECT rect = {LVIR_BOUNDS, colIdx};
        if (!SendMessageW(mControl, colIdx >= 0 ? LVM_GETSUBITEMRECT : LVM_GETITEMRECT, idx, (LPARAM)&rect)) {
            rect = {};
        }
        RECT selfRect = GetOutput(GetWindowRect, mControl);
        if (rect.top < 0 || rect.bottom > RectHeight(selfRect)) {
            rect = {};
        }
        rect.left += selfRect.left;
        rect.right += selfRect.left;
        rect.top += selfRect.top;
        rect.bottom += selfRect.top;
        return rect;
    }

    void EnsureVisible(int idx) {
        SendMessageW(mControl, LVM_ENSUREVISIBLE, idx, true);
    }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == NM_CUSTOMDRAW) {
            auto info = ((NMCUSTOMDRAW *)header);
            if (info->dwDrawStage == CDDS_PREPAINT && mRectChangedAction) {
                mRectChangedAction();
            }
        } else if (header->code == LVN_ITEMCHANGED && !mProgrammatic) {
            auto info = ((NMLISTVIEW *)header);
            if (mCheckAction) {
                auto newChecked = StateToChecked(info->uNewState);
                auto oldChecked = StateToChecked(info->uOldState);
                if (newChecked >= 0 && oldChecked >= 0 && newChecked != oldChecked) {
                    mCheckAction(info->iItem, (bool)newChecked);
                }
            }
            if (mActionSet && ((info->uNewState & LVIS_SELECTED) != (info->uOldState & LVIS_SELECTED)) && !mActionPosted) {
                mActionPosted = true;
                Post([this] {
                    DoAction();
                    mActionPosted = false;
                });
            }
        } else if (header->code == NM_CLICK) {
            DoClick(header, false, false);
        } else if (header->code == NM_DBLCLK) {
            DoClick(header, false, true);
        } else if (header->code == NM_RCLICK) {
            DoClick(header, true, false);
        } else if (header->code == NM_RDBLCLK) {
            DoClick(header, true, true);
        } else if (header->code == NM_RETURN) {
            DoKey(VK_RETURN);
        } else if (header->code == LVN_KEYDOWN) {
            *ptrResult = DoKey(((NMLVKEYDOWN *)header)->wVKey) ? 1 : 0;
        }
    }
};

class ListView : public ListViewBase {
    function<void(int)> mAction;

    void DoAction() override {
        if (mAction) {
            mAction(GetSelected());
        }
    }

public:
    ListView(HWND parent, intptr_t id, function<void(int idx)> &&action = {}) : ListViewBase(parent, id, LVS_SINGLESEL), mAction(move(action)) {
        mActionSet = (bool)mAction;
    }

    int GetSelected() {
        return (int)SendMessageW(mControl, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    }

    void SetSelected(int idx, bool action = true) {
        mProgrammatic = true;
        LVITEMW item;
        item.stateMask = LVIS_SELECTED;
        item.state = 0;
        SendMessageW(mControl, LVM_SETITEMSTATE, -1, (LPARAM)&item);
        item.state = LVIS_SELECTED;
        SendMessageW(mControl, LVM_SETITEMSTATE, idx, (LPARAM)&item);
        mProgrammatic = false;

        if (action && mAction) {
            mAction(idx);
        }
    }
};

class MultiListView : public ListViewBase {
    function<void(const vector<int> &indices)> mAction;

    void DoAction() override {
        if (mAction) {
            mAction(GetSelected());
        }
    }

public:
    MultiListView(HWND parent, intptr_t id, function<void(const vector<int> &indices)> &&action = {}) : ListViewBase(parent, id, 0), mAction(move(action)) {
        mActionSet = (bool)mAction;
    }

    vector<int> GetSelected() {
        vector<int> selected;
        int nextIdx = -1;
        while (true) {
            nextIdx = (int)SendMessageW(mControl, LVM_GETNEXTITEM, nextIdx, LVNI_SELECTED);
            if (nextIdx < 0) {
                break;
            }
            selected.push_back(nextIdx);
        }
        return selected;
    }

    void SetSelected(const vector<int> &indices, bool action = true) {
        mProgrammatic = true;
        LVITEMW item;
        item.stateMask = LVIS_SELECTED;
        item.state = 0;
        SendMessageW(mControl, LVM_SETITEMSTATE, -1, (LPARAM)&item);
        item.state = LVIS_SELECTED;
        for (int idx : indices) {
            SendMessageW(mControl, LVM_SETITEMSTATE, idx, (LPARAM)&item);
        }
        mProgrammatic = false;

        if (action && mAction) {
            mAction(indices);
        }
    }
};

using TreeNode = HTREEITEM;

class TreeViewBase : public HwndBase {
    function<void(TreeNode, bool)> mCheckAction;
    function<void(TreeNode, MaybeBool)> mPartialCheckAction;
    function<MaybeBool(TreeNode, MaybeBool)> mPartialToggle;
    function<void(TreeNode, bool, bool)> mClickAction;
    function<bool(int)> mKeyAction;

    int StateToChecked(int state) { return ((state >> 12) & 3) - 1; }
    int CheckedToState(bool checked) { return (checked ? 2 : 1) << 12; }
    int PartialCheckedToState(MaybeBool value) { return ((int)value + 1) << 12; }

    virtual void SetSelected(TreeNode ptr, bool action) = 0;

    void DoClick(bool right, bool dblclk) {
        POINT pos = GetOutput(GetCursorPos);
        RECT rect = GetOutput(GetWindowRect, mControl);

        TVHITTESTINFO test;
        test.pt = {pos.x - rect.left, pos.y - rect.top};
        TreeNode node = (TreeNode)SendMessageW(mControl, TVM_HITTEST, 0, (LPARAM)&test);
        if (right) {
            SetSelected(node, true);
        }

        if (mClickAction) {
            mClickAction(node, right, dblclk);
        }
    }
    bool DoKey(int vk) {
        return mKeyAction ? mKeyAction(vk) : false;
    }

protected:
    bool mProgrammatic = false;
    bool mAllowSelect = false;
    bool mActionSet = false;
    bool mActionPosted = false;

    virtual void DoAction() = 0;

    TreeViewBase(HWND parent, intptr_t id, int style) {
        mControl = CreateWindowW(WC_TREEVIEWW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_BORDER |
                                     TVS_HASBUTTONS | TVS_LINESATROOT | TVS_NONEVENHEIGHT | TVS_DISABLEDRAGDROP |
                                     TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | style,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        InitFont();
        SendMessageW(mControl, TVM_SETEXTENDEDSTYLE, 0, TVS_EX_DOUBLEBUFFER);
    }

    bool GetState(TreeNode node, int mask) {
        TVITEMW item;
        item.mask = TVIF_STATE;
        item.hItem = node;
        item.stateMask = mask;
        return SendMessageW(mControl, TVM_GETITEMW, 0, (LPARAM)&item) ? item.state : 0;
    }
    void SetState(TreeNode node, int mask, int state) {
        mProgrammatic = true;
        TVITEMW item;
        item.mask = TVIF_STATE;
        item.hItem = node;
        item.stateMask = mask;
        item.state = state;
        SendMessageW(mControl, TVM_SETITEMW, 0, (LPARAM)&item);
        mProgrammatic = false;
    }

public:
    void SetOnCheck(function<void(TreeNode node, bool state)> &&checkAction = {}) {
        mCheckAction = move(checkAction);
        SetWindowLongW(mControl, GWL_STYLE, GetWindowLongW(mControl, GWL_STYLE) | TVS_CHECKBOXES);
    }
    void SetOnPartialCheck(function<MaybeBool(TreeNode, MaybeBool)> &&toggle,
                           function<void(TreeNode node, MaybeBool state)> &&checkAction = {}, bool forChild = false) {
        int mask = forChild ? TVS_EX_EXCLUSIONCHECKBOXES : TVS_EX_PARTIALCHECKBOXES;
        mPartialToggle = move(toggle);
        mPartialCheckAction = move(checkAction);
        SendMessageW(mControl, TVM_SETEXTENDEDSTYLE, mask, mask);
    }
    void SetOnClick(function<void(TreeNode node, bool right, bool dblclk)> &&action) {
        mClickAction = move(action);
    }
    void SetOnKey(function<bool(int key)> &&action) {
        mKeyAction = move(action);
    }

    TreeNode GetFirstRoot() { return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_ROOT, 0); }
    TreeNode GetFirstChild(TreeNode node) { return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)node); }
    TreeNode GetNextSibling(TreeNode node) { return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)node); }
    TreeNode GetPrevSibling(TreeNode node) { return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_PREVIOUS, (LPARAM)node); }
    TreeNode GetParent(TreeNode node) { return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)node); }

    TreeNode GetLastChild(TreeNode node) {
        TreeNode child = GetFirstChild(node);
        if (!child) {
            return nullptr;
        }

        while (true) {
            TreeNode next = GetNextSibling(child);
            if (next) {
                child = next;
            } else {
                return child;
            }
        }
    }

    tuple<bool, TreeNode, TreeNode> GetAdjacentInsertPos(TreeNode node, bool down) {
        if (down) {
            TreeNode adj = GetNextSibling(node);
            if (adj) {
                if (HasChildren(adj)) {
                    return {true, adj, nullptr};
                } else {
                    return {true, GetParent(adj), adj};
                }
            }

            TreeNode parent = GetParent(node);
            if (parent) {
                return {true, GetParent(parent), parent};
            } else {
                return {false, nullptr, nullptr};
            }
        } else {
            TreeNode adj = GetPrevSibling(node);
            if (adj) {
                if (HasChildren(adj)) {
                    return {true, adj, GetLastChild(adj)};
                } else {
                    return {true, GetParent(adj), GetPrevSibling(adj)};
                }
            }

            TreeNode parent = GetParent(node);
            if (parent) {
                return {true, GetParent(parent), GetPrevSibling(parent)};
            } else {
                return {false, nullptr, nullptr};
            }
        }
    }

    void *GetData(TreeNode node) {
        TVITEMW item;
        item.mask = TVIF_PARAM;
        item.hItem = node;
        return SendMessageW(mControl, TVM_GETITEMW, 0, (LPARAM)&item) ? (void *)item.lParam : nullptr;
    }
    void SetData(TreeNode node, void *data) {
        TVITEMW item;
        item.mask = TVIF_PARAM;
        item.hItem = node;
        item.lParam = (LPARAM)data;
        SendMessageW(mControl, TVM_SETITEMW, 0, (LPARAM)&item);
    }

    TreeNode Add(TreeNode parent, const wchar_t *str, void *data = nullptr) { return Insert(parent, TVI_LAST, str, data); }
    TreeNode Insert(TreeNode parent, TreeNode prev, const wchar_t *str, void *data = nullptr) {
        TVINSERTSTRUCTW ins;
        ins.hParent = parent;
        ins.hInsertAfter = prev ? prev : TVI_FIRST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = (wchar_t *)str;
        ins.item.lParam = (LPARAM)data;
        return (TreeNode)SendMessageW(mControl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    }

    void Remove(TreeNode node) {
        mAllowSelect = true;
        SendMessageW(mControl, TVM_DELETEITEM, 0, (LPARAM)node);
        mAllowSelect = false;
    }
    void Clear() { Remove(nullptr); }

    void Set(TreeNode node, const wchar_t *str) {
        TVITEMW item;
        item.mask = TVIF_TEXT;
        item.hItem = node;
        item.pszText = (wchar_t *)str;
        SendMessageW(mControl, TVM_SETITEMW, 0, (LPARAM)&item);
    }

    bool GetExpanded(TreeNode node) { return GetState(node, TVIS_EXPANDED); }
    void SetExpanded(TreeNode node, bool expand = true) {
        SendMessageW(mControl, TVM_EXPAND, expand ? TVE_EXPAND : TVE_COLLAPSE, (LPARAM)node);
    }

    bool GetChecked(TreeNode node) {
        return StateToChecked(GetState(node, TVIS_STATEIMAGEMASK)) == 1;
    }
    void SetChecked(TreeNode node, bool checked = true, bool action = true) {
        SetState(node, TVIS_STATEIMAGEMASK, CheckedToState(checked));

        if (action && mCheckAction) {
            mCheckAction(node, checked);
        }
    }

    MaybeBool GetPartialChecked(TreeNode node) {
        auto checked = StateToChecked(GetState(node, TVIS_STATEIMAGEMASK));
        return checked < 0 ? MaybeBool::Partial : (MaybeBool)checked;
    }
    void SetPartialChecked(TreeNode node, MaybeBool checked, bool action = true) {
        SetState(node, TVIS_STATEIMAGEMASK, PartialCheckedToState(checked));

        if (action && mPartialCheckAction) {
            mPartialCheckAction(node, checked);
        }
    }

    bool HasChildren(TreeNode node) {
        TVITEMW item;
        item.mask = TVIF_CHILDREN;
        item.hItem = node;
        return SendMessageW(mControl, TVM_GETITEMW, 0, (LPARAM)&item) ? item.cChildren != 0 : false;
    }
    void ForceHasChildren(TreeNode node, bool value = true) {
        TVITEMW item;
        item.mask = TVIF_CHILDREN;
        item.hItem = node;
        item.cChildren = value ? 1 : 0;
        SendMessageW(mControl, TVM_SETITEMW, 0, (LPARAM)&item);
    }

    RECT GetRect(TreeNode node) {
        RECT rect;
        *(TreeNode *)&rect = node;
        if (!SendMessageW(mControl, TVM_GETITEMRECT, false, (LPARAM)&rect)) {
            rect = {};
        }
        RECT selfRect = GetOutput(GetWindowRect, mControl);
        if (rect.top < 0 || rect.bottom > RectHeight(selfRect)) {
            rect = {};
        }
        rect.left += selfRect.left;
        rect.right += selfRect.left;
        rect.top += selfRect.top;
        rect.bottom += selfRect.top;
        return rect;
    }

    void EnsureVisible(TreeNode node) {
        SendMessageW(mControl, TVM_ENSUREVISIBLE, 0, (LPARAM)node);
    }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == TVN_ITEMCHANGEDW && !mProgrammatic) {
            auto info = ((NMTVITEMCHANGE *)header);
            if (mCheckAction) {
                auto newChecked = StateToChecked(info->uStateNew);
                auto oldChecked = StateToChecked(info->uStateOld);
                if (newChecked >= 0 && oldChecked >= 0 && newChecked != oldChecked) {
                    mCheckAction(info->hItem, (bool)newChecked);
                }
            }
            if (mPartialCheckAction) {
                auto newChecked = StateToChecked(info->uStateNew);
                auto oldChecked = StateToChecked(info->uStateOld);
                if (newChecked >= 0 && oldChecked >= 0 && newChecked != oldChecked) {
                    int newNewChecked = (int)mPartialToggle(info->hItem, (MaybeBool)oldChecked);
                    if (newChecked != newNewChecked) {
                        SetPartialChecked(info->hItem, (MaybeBool)newNewChecked, false);
                    }
                    mPartialCheckAction(info->hItem, (MaybeBool)newNewChecked);
                }
            }
            if (mActionSet && ((info->uStateNew & TVIS_SELECTED) != (info->uStateOld & TVIS_SELECTED)) && !mActionPosted) {
                mActionPosted = true;
                Post([this] {
                    DoAction();
                    mActionPosted = false;
                });
            }
        } else if (header->code == TVN_SELCHANGINGW) {
            auto info = ((NMTREEVIEWW *)header);
            if (info->action != TVC_BYKEYBOARD && info->action != TVC_BYMOUSE && !mAllowSelect) {
                *ptrResult = true;
            }
        } else if (header->code == NM_CLICK) {
            DoClick(false, false);
        } else if (header->code == NM_DBLCLK) {
            DoClick(false, true);
        } else if (header->code == NM_RCLICK) {
            DoClick(true, false);
        } else if (header->code == NM_RDBLCLK) {
            DoClick(true, true);
        } else if (header->code == NM_RETURN) {
            DoKey(VK_RETURN);
        } else if (header->code == TVN_KEYDOWN) {
            *ptrResult = DoKey(((NMTVKEYDOWN *)header)->wVKey) ? 1 : 0;
        }
    }
};

class TreeView : public TreeViewBase {
    function<void(TreeNode)> mAction;

    void DoAction() override {
        if (mAction) {
            mAction(GetSelected());
        }
    }

public:
    TreeView(HWND parent, intptr_t id, function<void(TreeNode node)> &&action = {}) : TreeViewBase(parent, id, 0), mAction(move(action)) {
        mActionSet = (bool)mAction;
    }

    TreeNode GetSelected() {
        return (TreeNode)SendMessageW(mControl, TVM_GETNEXTITEM, TVGN_CARET, 0);
    }

    void SetSelected(TreeNode ptr, bool action = true) {
        mProgrammatic = mAllowSelect = true;
        SendMessageW(mControl, TVM_SELECTITEM, TVGN_CARET, (LPARAM)ptr);
        mProgrammatic = mAllowSelect = false;

        if (action && mAction) {
            mAction(ptr);
        }
    }
};

class ProgressBar : public HwndBase {
    int mIdealHeight = -1;
    int mRangeMax = 0;

public:
    ProgressBar(HWND parent, intptr_t id, int range) {
        mControl = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH,
                                 0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        SetRange(range);
    }

    void SetImmediate() {
        SetWindowTheme(mControl, L"", L"");
    }

    void SetRange(int range) { SetRange(0, range); }
    void SetRange(int min, int max) {
        SendMessageW(mControl, PBM_SETRANGE32, min, max);
        mRangeMax = max;
    }

    int Get() { return (int)SendMessageW(mControl, PBM_GETPOS, 0, 0); }
    void Set(int pos) { SendMessageW(mControl, PBM_SETPOS, pos, 0); }
    void Add(int delta = 1) { SendMessageW(mControl, PBM_DELTAPOS, delta, 0); }

    bool Done() { return Get() == mRangeMax; }

    SIZE GetSize(SizeType type) override {
        if (mIdealHeight < 0) {
            mIdealHeight = DialogUnitsToSize({0, 11}).cy;
        }
        return SIZE{Control::GetSize(type).cx, mIdealHeight};
    }
};

class Timer : public UiBase {
    HWND mParent;
    UINT_PTR mId;
    function<void()> mAction;
    double mPeriod;
    bool mStarted = false;

public:
    Timer(HWND parent, intptr_t id, double period, bool start, function<void()> &&action)
        : mParent(parent), mId(id), mPeriod(period), mAction(move(action)) {
        if (start) {
            Start();
        }
    }
    Timer(HWND parent, intptr_t id, double period, function<void()> &&action)
        : Timer(parent, id, period, true, move(action)) {}
    Timer(HWND parent, intptr_t id, function<void()> &&action)
        : Timer(parent, id, 1, false, move(action)) {}
    ~Timer() { End(); }

    bool WasStarted() { return mStarted; }
    void Start(double period) {
        DWORD periodMs = (DWORD)ceil(period * 1000);
        SetTimer(mParent, mId, periodMs, nullptr);
        mStarted = true;
        mPeriod = period;
    }
    void Start() {
        Start(mPeriod);
    }
    void End() {
        if (mStarted) {
            KillTimer(mParent, mId);
            mStarted = false;
        }
    }

    void OnCommand(WORD) override { mAction(); }
};

class PassiveTimer {
    ULONGLONG mTicks = 0;

public:
    PassiveTimer() : mTicks(GetTickCount64()) {}

    double TakeElapsed() {
        auto oldTicks = mTicks;
        mTicks = GetTickCount64();
        return (double)(mTicks - oldTicks) / 1000;
    }
};

class Keyboard {
public:
    static int Modifiers(int vk = 0) {
        return (GetKeyState(VK_SHIFT) < 0 && vk != VK_SHIFT ? FSHIFT : 0) |
               (GetKeyState(VK_CONTROL) < 0 && vk != VK_CONTROL ? FCONTROL : 0) |
               (GetKeyState(VK_MENU) < 0 && vk != VK_MENU ? FALT : 0);
    }
};

class EditKey : public EditLine {
    UniquePtr<ProgressBar> mProgressBar;
    function<void(int, int, int, int)> mVkAction;
    function<void(int, int)> mWheelAction;
    tuple<int, int, int> mPrevVk = {};
    bool mTrackMouseLeave = false;

    void Abort() {
        mPrevVk = {};
        EndProgressIfNeeded();
    }

    LRESULT OnVk(bool down, int vk, int scan = 0, int ext = 0) {
        tuple<int, int, int> vkTuple = {vk, scan, ext};

        if (down) {
            if (GetFocus() == mControl) {
                mPrevVk = vkTuple;
                StartProgressIfNeeded(vk);
            } else {
                SetFocus();
            }
        } else {
            if (mPrevVk == vkTuple && !mProgressBar && mVkAction) {
                mVkAction(vk, scan, ext, Keyboard::Modifiers(vk));
            }
            Abort();
        }
        return 0;
    }

    LRESULT OnWheel(int dx, int dy) {
        if (GetFocus() == mControl && mWheelAction) {
            mWheelAction(dx, dy);
        }

        Abort();
        return 0;
    }

    void StartProgressIfNeeded(int vk) {
        switch (vk) {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_LBUTTON:
            EndProgressIfNeeded();

            mProgressBar = UniquePtr<ProgressBar>::New(::GetParent(mControl), 0, 60);
            mProgressBar->SetImmediate();
            mProgressBar->SetParent(this);
            mProgressBar->OnSetRect(GetOutput(GetClientRect, mControl));
            mProgressBar->Set(1);

            SetTimer(mControl, (UINT_PTR)this, USER_TIMER_MINIMUM, nullptr);

            if (vk == VK_LBUTTON) {
                TrackMouseLeave(true);
                mTrackMouseLeave = true;
            }
            break;
        }
    }

    void EndProgressIfNeeded() {
        if (mProgressBar) {
            KillTimer(mControl, (UINT_PTR)this);
            mProgressBar = nullptr;

            if (mTrackMouseLeave) {
                TrackMouseLeave(false);
                mTrackMouseLeave = false;
            }
        }
    }

    virtual LRESULT OnSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            bool down = !(HIWORD(lParam) & KF_UP);
            bool prevDown = (HIWORD(lParam) & KF_REPEAT);
            if (down != prevDown) {
                OnVk(down, (int)wParam, HIWORD(lParam) & 0xff, HIWORD(lParam) & KF_EXTENDED);
            }
        }
            return 0;
        case WM_CHAR:
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            return OnVk(msg != WM_LBUTTONUP, VK_LBUTTON);
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_RBUTTONUP:
            return OnVk(msg != WM_RBUTTONUP, VK_RBUTTON);
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_MBUTTONUP:
            return OnVk(msg != WM_MBUTTONUP, VK_MBUTTON);
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
        case WM_XBUTTONUP:
            return OnVk(msg != WM_XBUTTONUP, HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);

        case WM_MOUSELEAVE:
            if (mTrackMouseLeave) {
                Abort();
            }
            break;

        case WM_MOUSEWHEEL:
            return OnWheel(0, (short)HIWORD(wParam));
        case WM_MOUSEHWHEEL:
            return OnWheel((short)HIWORD(wParam), 0);

        case WM_TIMER:
            if (wParam == (UINT_PTR)this && mProgressBar) {
                mProgressBar->Add();
                if (mProgressBar->Done()) {
                    auto [vk, scan, ext] = mPrevVk;
                    if (mVkAction) {
                        mVkAction(vk, scan, ext, Keyboard::Modifiers(vk));
                    }
                    Abort();
                }
                return 0;
            }
            break;

        case WM_PAINT:
            if (mProgressBar) {
                ValidateRect(mControl, nullptr);
                return 0;
            }
            break;

        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        }
        return HwndBase::OnSubclassMessage(hwnd, msg, wParam, lParam);
    }

    void OnSetActive(bool active, void *root, void *prev) override {
        if (!active) {
            Abort();
        }
    }

public:
    EditKey(HWND parent, intptr_t id, function<void(int key, int scan, int ext, int mods)> &&vkAction = {},
            function<void(int x, int y)> &&wheelAction = {}) : EditLine(parent, id), mVkAction(move(vkAction)), mWheelAction(move(wheelAction)) {
        Subclass();
    }
};

class Dynamic : public Control {
    Control *mCurrent;
    bool mActive = false;

public:
    Dynamic(HWND, intptr_t, Control *child = nullptr) : mCurrent(child) {}

    Control *GetChild() { return mCurrent; }

    void SetChild(Control *child, void *root = nullptr) {
        if (child != mCurrent) {
            root = root ? root : this;

            if (mCurrent && mCurrent->GetParent() == this) {
                if (mActive) {
                    mCurrent->OnSetActive(false, root, child);
                }
                mCurrent->SetParent(nullptr);
            }

            auto oldCurrent = mCurrent;
            mCurrent = child;
            if (mCurrent) {
                if (mActive) {
                    mCurrent->OnSetActive(true, root, oldCurrent);
                    Control *initFocus = mCurrent->InitialFocus();
                    if (initFocus) {
                        initFocus->SetFocus();
                    }
                }
                mCurrent->SetParent(this);
            }

            OnChildResize();
        }
    }

    Control *InitialFocus() override {
        return mCurrent ? mCurrent->InitialFocus() : nullptr;
    }

    SIZE GetSize(SizeType type) override {
        if (mCurrent) {
            return mCurrent->GetSize(type);
        } else {
            return SIZE{0, 0};
        }
    }

    void OnSetRect(RECT rect, RectType type) override {
        if (mCurrent) {
            mCurrent->OnSetRect(rect, type);
        }
    }

    bool OnShow(bool show, HideType type) override {
        bool changed = Control::OnShow(show, type);
        if (changed && mCurrent) {
            mCurrent->OnShow(show, HideType::FromParent);
        }
        return changed;
    }

    bool OnEnable(bool enable, HideType type) override {
        bool changed = Control::OnEnable(enable, type);
        if (changed && mCurrent) {
            mCurrent->OnEnable(enable, HideType::FromParent);
        }
        return changed;
    }

    void OnSetActive(bool active, void *root, void *prev) override {
        mActive = active;
        if (mCurrent) {
            mCurrent->OnSetActive(active, root, prev);
        }
    }
};

class Union : public Dynamic {
    vector<Control *> mChildren;
    bool mNeedInvalidate = false;

    void OnChildResize(RectType type = RectType::Real) override {
        mNeedInvalidate = false;
        Control::OnChildResize(type);
    }

    void InvalidateSize() {
        if (mNeedInvalidate) {
            OnChildResize();
        }
    }

public:
    Union(HWND parent, intptr_t id) : Dynamic(parent, id) {}

    SIZE GetSize(SizeType type) override {
        mNeedInvalidate = true;
        SIZE size = {0, 0};
        for (auto &child : mChildren) {
            auto childSize = child->GetSize(type);
            size.cx = max(size.cx, childSize.cx);
            size.cy = max(size.cy, childSize.cy);
        }
        return size;
    }

    void Add(Control *control) { Insert(mChildren.size(), control); }

    void Insert(size_t idx, Control *control) {
        mChildren.insert(mChildren.begin() + idx, control);
        InvalidateSize();
    }

    void Remove(size_t idx) {
        if (GetChild() == mChildren[idx]) {
            SetChild(nullptr);
        }

        mChildren.erase(mChildren.begin() + idx);
        InvalidateSize();
    }

    void Clear() {
        SetChild(nullptr);
        mChildren.clear();
        InvalidateSize();
    }

    size_t Count() { return mChildren.size(); }
    Control *Get(size_t idx) { return idx < Count() ? mChildren[idx] : nullptr; }
    intptr_t GetIndex(Control *control) { return ::Find(mChildren, control); }
};

template <typename TBuddy>
class TabBase : public BuddyBase<HwndBase, TBuddy> {
    function<void(int)> mAction;

    void OnSetRect(RECT rect, HwndBase::RectType type) override {
        RECT tabRect = rect;
        HwndBase::OnSetRect(tabRect, type); // needed for TCM_ADJUSTRECT

        SendMessageW(this->mControl, TCM_ADJUSTRECT, false, (LPARAM)&rect);
        this->mBuddy.OnSetRect(rect, type);

        tabRect.bottom = rect.top;
        HwndBase::OnSetRect(tabRect, type); // to avoid it taking over the background
    }

public:
    TabBase(HWND parent, intptr_t id, function<void(int idx)> &&action) : BuddyBase<HwndBase, TBuddy>(parent, id), mAction(move(action)) {
        this->mControl = CreateWindowW(WC_TABCONTROLW, nullptr,
                                       WS_TABSTOP | WS_CHILD | WS_CLIPSIBLINGS | TCS_MULTILINE,
                                       0, 0, 0, 0, parent, (HMENU)id, nullptr, nullptr);
        this->InitFont();
    }

    int Insert(int idx, const wchar_t *str) {
        TCITEMW item;
        item.mask = TCIF_TEXT;
        item.pszText = (wchar_t *)str;
        return (int)SendMessageW(this->mControl, TCM_INSERTITEMW, idx, (LPARAM)&item);
    }
    int Add(const wchar_t *str) { return Insert(INT_MAX, str); }

    void Remove(int idx) { SendMessageW(this->mControl, TCM_DELETEITEM, idx, 0); }
    void Clear() { SendMessageW(this->mControl, TCM_DELETEALLITEMS, 0, 0); }

    int GetSelected() { return (int)SendMessageW(this->mControl, TCM_GETCURSEL, 0, 0); }

    void SetSelected(int idx, bool action = true) {
        SendMessageW(this->mControl, TCM_SETCURSEL, idx, 0);
        if (action && mAction) {
            mAction(idx);
        }
    }

    void OnNotify(NMHDR *header, LRESULT *ptrResult) override {
        if (header->code == TCN_SELCHANGE && mAction) {
            mAction(GetSelected());
        }
    }

    Control *InitialFocus() override { return this->mBuddy.InitialFocus(); }
};

class UnionTab : public TabBase<Union> {
    virtual SIZE GetSize(SizeType type) {
        RECT rect = ToRect(mBuddy.GetSize(type));
        SendMessageW(mControl, TCM_ADJUSTRECT, true, (LPARAM)&rect);
        return RectSize(rect);
    }

public:
    UnionTab(HWND parent, intptr_t id) : TabBase(parent, id, [this](int i) {
                                             mBuddy.SetChild(mBuddy.Get(i));
                                         }) {}

    void Add(const wchar_t *str, Control *child) {
        Insert((int)mBuddy.Count(), str, child);
    }

    void Insert(int idx, const wchar_t *str, Control *child) {
        mBuddy.Insert(idx, child);
        if (!mBuddy.GetChild()) {
            mBuddy.SetChild(child);
        }

        TabBase::Insert(idx, str);
    }

    void Remove(int idx) {
        mBuddy.Remove(idx);
        if (!mBuddy.GetChild()) {
            auto sel = mBuddy.Get(idx);
            if (!sel && idx > 0) {
                sel = mBuddy.Get(idx - 1);
            }
            mBuddy.SetChild(sel);
        }

        TabBase::Remove(idx);
    }

    void Clear() {
        mBuddy.Clear();
        TabBase::Clear();
    }

    Control *GetSelected() {
        UINT i = TabBase::GetSelected();
        return mBuddy.Get(i);
    }
    void SetSelected(Control *value) {
        intptr_t i = mBuddy.GetIndex(value);
        if (i >= 0) {
            TabBase::SetSelected((int)i);
        }
    }
};

template <typename TSelf, typename TBase>
class LayoutBase : public TBase {
protected:
    bool OnShow(bool show, TBase::HideType type) override {
        bool changed = Control::OnShow(show, type);
        if (changed) {
            ((TSelf *)this)->ForEachChild([&](Control *child) {
                child->OnShow(show, TBase::HideType::FromParent);
            });
        }
        return changed;
    }

    bool OnEnable(bool enable, TBase::HideType type) override {
        bool changed = Control::OnEnable(enable, type);
        if (changed) {
            ((TSelf *)this)->ForEachChild([&](Control *child) {
                child->OnEnable(enable, TBase::HideType::FromParent);
            });
        }
        return changed;
    }

    void OnSetActive(bool active, void *root, void *prev) override {
        ((TSelf *)this)->ForEachChild([&](Control *child) {
            child->OnSetActive(active, root, prev);
        });
    }
};

class Layout : public LayoutBase<Layout, Control> {
    enum class LayoutOp {
        OuterMargins,
        OnLeft,
        OnRight,
        OnTop,
        OnBottom,
        OnRemaining,
    };

    enum class LayoutAlign {
        Full,
        Start,
        Middle,
        End,
    };

    struct LayoutInstr {
        LayoutOp Op;
        LayoutAlign Align;
        Control *Target;
        LONG Size, Offset, Margin;
    };

    vector<LayoutInstr> mInstrs;
    SIZE mMargin;
    SIZE mIdealSize = {-1, -1};
    SIZE mMinSize = {-1, -1};

    static constexpr int ProportionScale = 0x10000000;
    bool IsProportion(LONG prop) { return prop <= -ProportionScale; }
    LONG MulProportion(LONG value, LONG prop) { return MulDiv(value, -prop - ProportionScale, ProportionScale); }
    LONG DivProportion(LONG value, LONG prop) { return MulDiv(value, ProportionScale, -prop - ProportionScale); }
    LONG ComplProportion(LONG prop) { return -ProportionScale * 3 - prop; }

    struct SetRectArgs {
        RectType Type;
        RECT Rest;
    };

    void DoSetRectInstrs(SetRectArgs &a) {
        for (LayoutInstr instr : mInstrs) {
            switch (instr.Op) {
            case LayoutOp::OuterMargins:
                DoSetRectOuterMargins(a);
                break;
            case LayoutOp::OnLeft:
                DoSetRectOnLeft(instr, a);
                break;
            case LayoutOp::OnRight:
                DoSetRectOnRight(instr, a);
                break;
            case LayoutOp::OnTop:
                DoSetRectOnTop(instr, a);
                break;
            case LayoutOp::OnBottom:
                DoSetRectOnBottom(instr, a);
                break;
            case LayoutOp::OnRemaining:
                DoSetRectOnRemaining(instr, a);
                break;
            }
        }
    }

    void DoSetRectOuterMargins(SetRectArgs &a) {
        a.Rest.left += mMargin.cx;
        a.Rest.top += mMargin.cy;
        a.Rest.right -= mMargin.cx;
        a.Rest.bottom -= mMargin.cy;
        a.Rest.right = max(a.Rest.right, a.Rest.left);
        a.Rest.bottom = max(a.Rest.bottom, a.Rest.top);
    }

    bool GetAlignAdjust(LayoutAlign align, int space, int size, int offset, int *outStart, int *outEnd) {
        switch (align) {
        case LayoutAlign::Start:
            *outStart = offset;
            *outEnd = space - size - offset;
            return true;
        case LayoutAlign::Middle:
            *outStart = (space - size) / 2;
            *outEnd = (space - size + 1) / 2;
            return true;
        case LayoutAlign::End:
            *outStart = space - size - offset;
            *outEnd = offset;
            return true;
        default:
            return false;
        }
    }

    void DoSetRectCommonVert(LayoutInstr &i, SetRectArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cy;
        }

        if (IsProportion(i.Size)) {
            i.Size = MulProportion(RectHeight(a.Rest), i.Size);
        } else if (i.Size < 0) {
            i.Size = i.Target->GetSize(SizeType::Ideal).cy;
        }
        i.Size = min(i.Size, RectHeight(a.Rest));
    }
    void DoSetRectCommonHorz(LayoutInstr &i, SetRectArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cx;
        }

        if (IsProportion(i.Size)) {
            i.Size = MulProportion(RectWidth(a.Rest), i.Size);
        } else if (i.Size < 0) {
            i.Size = i.Target->GetSize(SizeType::Ideal).cx;
        }
        i.Size = min(i.Size, RectWidth(a.Rest));
    }

    void DoSetRectOnTop(LayoutInstr &i, SetRectArgs &a) {
        DoSetRectCommonVert(i, a);

        int leftAdj, rightAdj;
        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else if (i.Align == LayoutAlign::Full) {
                i.Target->OnSetRect(RECT{a.Rest.left + i.Offset, a.Rest.top, a.Rest.right - i.Offset, a.Rest.top + i.Size}, a.Type);
            } else if (GetAlignAdjust(i.Align, RectWidth(a.Rest), i.Target->GetSize(SizeType::Ideal).cx, i.Offset, &leftAdj, &rightAdj)) {
                i.Target->OnSetRect(RECT{a.Rest.left + leftAdj, a.Rest.top, a.Rest.right - rightAdj, a.Rest.top + i.Size}, a.Type);
            }
        }

        a.Rest.top += i.Size + i.Margin;
        a.Rest.top = min(a.Rest.top, a.Rest.bottom);
    }
    void DoSetRectOnBottom(LayoutInstr &i, SetRectArgs &a) {
        DoSetRectCommonVert(i, a);

        int leftAdj, rightAdj;
        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else if (i.Align == LayoutAlign::Full) {
                i.Target->OnSetRect(RECT{a.Rest.left + i.Offset, a.Rest.bottom - i.Size, a.Rest.right - i.Offset, a.Rest.bottom}, a.Type);
            } else if (GetAlignAdjust(i.Align, RectWidth(a.Rest), i.Target->GetSize(SizeType::Ideal).cx, i.Offset, &leftAdj, &rightAdj)) {
                i.Target->OnSetRect(RECT{a.Rest.left + leftAdj, a.Rest.bottom - i.Size, a.Rest.right - rightAdj, a.Rest.bottom}, a.Type);
            }
        }

        a.Rest.bottom -= i.Size + i.Margin;
        a.Rest.bottom = max(a.Rest.bottom, a.Rest.top);
    }
    void DoSetRectOnLeft(LayoutInstr &i, SetRectArgs &a) {
        DoSetRectCommonHorz(i, a);

        int topAdj, bottomAdj;
        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else if (i.Align == LayoutAlign::Full) {
                i.Target->OnSetRect(RECT{a.Rest.left, a.Rest.top + i.Offset, a.Rest.left + i.Size, a.Rest.bottom - i.Offset}, a.Type);
            } else if (GetAlignAdjust(i.Align, RectHeight(a.Rest), i.Target->GetSize(SizeType::Ideal).cy, i.Offset, &topAdj, &bottomAdj)) {
                i.Target->OnSetRect(RECT{a.Rest.left, a.Rest.top + topAdj, a.Rest.left + i.Size, a.Rest.bottom - bottomAdj}, a.Type);
            }
        }

        a.Rest.left += i.Size + i.Margin;
        a.Rest.left = min(a.Rest.left, a.Rest.right);
    }
    void DoSetRectOnRight(LayoutInstr &i, SetRectArgs &a) {
        DoSetRectCommonHorz(i, a);

        int topAdj, bottomAdj;
        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else if (i.Align == LayoutAlign::Full) {
                i.Target->OnSetRect(RECT{a.Rest.right - i.Size, a.Rest.top + i.Offset, a.Rest.right, a.Rest.bottom - i.Offset}, a.Type);
            } else if (GetAlignAdjust(i.Align, RectHeight(a.Rest), i.Target->GetSize(SizeType::Ideal).cy, i.Offset, &topAdj, &bottomAdj)) {
                i.Target->OnSetRect(RECT{a.Rest.right - i.Size, a.Rest.top + topAdj, a.Rest.right, a.Rest.bottom - bottomAdj}, a.Type);
            }
        }

        a.Rest.right -= i.Size + i.Margin;
        a.Rest.right = max(a.Rest.right, a.Rest.left);
    }

    void DoSetRectOnRemaining(LayoutInstr &i, SetRectArgs &a) {
        if (!i.Target || !i.Target->IsDisplayed()) {
            return;
        }

        i.Target->OnSetRect(a.Rest, a.Type);

        a.Rest.right = a.Rest.left;
        a.Rest.bottom = a.Rest.top;
    }

    struct GetSizeArgs {
        SizeType Type;
        SIZE Total;
        bool NeedMargin;
    };

    void DoGetSizeInstrs(GetSizeArgs &a) {
        for (LayoutInstr instr : views::reverse(mInstrs)) {
            switch (instr.Op) {
            case LayoutOp::OuterMargins:
                DoGetSizeOuterMargins(a);
                break;
            case LayoutOp::OnLeft:
            case LayoutOp::OnRight:
                DoGetSizeOnHorz(instr, a);
                break;
            case LayoutOp::OnTop:
            case LayoutOp::OnBottom:
                DoGetSizeOnVert(instr, a);
                break;
            case LayoutOp::OnRemaining:
                DoGetSizeOnRemaining(instr, a);
                break;
            }
        }
    }

    void DoGetSizeOuterMargins(GetSizeArgs &a) {
        a.Total.cx += mMargin.cx * 2;
        a.Total.cy += mMargin.cy * 2;
        a.NeedMargin = true;
    }

    void DoGetSizeOnHorz(LayoutInstr &i, GetSizeArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cx;
        }

        if (a.NeedMargin) {
            a.Total.cx += i.Margin;
        } else {
            a.NeedMargin = true;
        }

        if (IsProportion(i.Size)) {
            i.Size = DivProportion(a.Total.cx, ComplProportion(i.Size)) - a.Total.cx;
        } else if (i.Size < 0) {
            i.Size = i.Target->GetSize(SizeType::Ideal).cx;
        }

        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else {
                a.Total.cy = max(a.Total.cy, i.Target->GetSize(a.Type).cy + i.Offset * 2);
            }
        }

        a.Total.cx += i.Size;
    }
    void DoGetSizeOnVert(LayoutInstr &i, GetSizeArgs &a) {
        if (i.Margin < 0) {
            i.Margin = mMargin.cy;
        }

        if (a.NeedMargin) {
            a.Total.cy += i.Margin;
        } else {
            a.NeedMargin = true;
        }

        if (IsProportion(i.Size)) {
            i.Size = DivProportion(a.Total.cy, ComplProportion(i.Size)) - a.Total.cy;
        } else if (i.Size < 0) {
            i.Size = i.Target->GetSize(SizeType::Ideal).cy;
        }

        if (i.Target) {
            if (!i.Target->IsDisplayed()) {
                return;
            } else {
                a.Total.cx = max(a.Total.cx, i.Target->GetSize(a.Type).cx + i.Offset * 2);
            }
        }

        a.Total.cy += i.Size;
    }

    void DoGetSizeOnRemaining(LayoutInstr &i, GetSizeArgs &a) {
        if (!i.Target || !i.Target->IsDisplayed()) {
            return;
        }

        a.Total = i.Target->GetSize(a.Type);
        a.NeedMargin = true;
    }

    void OnChildResize(RectType type = RectType::Real) override {
        mMinSize = mIdealSize = {-1, -1};
        Control::OnChildResize(type);
    }

    void InvalidateSize() {
        if (mIdealSize.cx >= 0) {
            OnChildResize();
        }
    }

    void AddChild(Control *ctrl, LayoutOp op, LayoutAlign align, LONG height, LONG offset, LONG margin) {
        if (ctrl) {
            ctrl->SetParent(this);
        }
        mInstrs.push_back({op, align, ctrl, DpiScale(height), DpiScale(offset), DpiScale(margin)});
        InvalidateSize();
    }

    template <typename TOp>
    void ForEachChild(TOp &&op) {
        for (auto &instr : mInstrs) {
            if (instr.Target) {
                op(instr.Target);
            }
        }
    }

    friend class LayoutBase<Layout, Control>;

public:
    Layout(HWND, intptr_t, bool outerMargins = false, SIZE margin = {8, 8}) : mMargin(DpiScale(margin)) {
        if (outerMargins) {
            mInstrs.push_back({LayoutOp::OuterMargins});
        }
    }

    Layout(HWND h, intptr_t i, SIZE margin) : Layout(h, i, false, margin) {}

    SIZE GetSize(SizeType type) override {
        SIZE *cachePtr = type == SizeType::Min ? &mMinSize : type == SizeType::Ideal ? &mIdealSize
                                                                                     : nullptr;

        if (cachePtr && cachePtr->cx >= 0) {
            return *cachePtr;
        }

        GetSizeArgs args = {type};
        DoGetSizeInstrs(args);

        if (cachePtr) {
            *cachePtr = args.Total;
        }
        return args.Total;
    }

    void OnSetRect(RECT rect, RectType type) override {
        SetRectArgs args = {type, rect};
        DoSetRectInstrs(args);
    }

    static int Proportion(double value) // pass to width/height
    {
        return -Clamp(value * ProportionScale, 0, ProportionScale) - ProportionScale;
    }

    void Clear() {
        int numInitial = mInstrs.size() && mInstrs[0].Op == LayoutOp::OuterMargins ? 1 : 0;
        mInstrs.erase(mInstrs.begin() + numInitial, mInstrs.end());
        InvalidateSize();
    }

    void AddTop(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnTop, LayoutAlign::Full, height, offset, margin); }
    void AddBottom(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnBottom, LayoutAlign::Full, height, offset, margin); }
    void AddLeft(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnLeft, LayoutAlign::Full, width, offset, margin); }
    void AddRight(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnRight, LayoutAlign::Full, width, offset, margin); }

    void AddTopLeft(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnTop, LayoutAlign::Start, height, offset, margin); }
    void AddTopMiddle(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnTop, LayoutAlign::Middle, height, offset, margin); }
    void AddTopRight(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnTop, LayoutAlign::End, height, offset, margin); }
    void AddBottomLeft(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnBottom, LayoutAlign::Start, height, offset, margin); }
    void AddBottomMiddle(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnBottom, LayoutAlign::Middle, height, offset, margin); }
    void AddBottomRight(Control *ctrl, LONG height = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnBottom, LayoutAlign::End, height, offset, margin); }
    void AddLeftTop(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnLeft, LayoutAlign::Start, width, offset, margin); }
    void AddLeftMiddle(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnLeft, LayoutAlign::Middle, width, offset, margin); }
    void AddLeftBottom(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnLeft, LayoutAlign::End, width, offset, margin); }
    void AddRightTop(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnRight, LayoutAlign::Start, width, offset, margin); }
    void AddRightMiddle(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnRight, LayoutAlign::Middle, width, offset, margin); }
    void AddRightBottom(Control *ctrl, LONG width = -1, LONG offset = 0, LONG margin = -1) { AddChild(ctrl, LayoutOp::OnRight, LayoutAlign::End, width, offset, margin); }

    void AddRemaining(Control *ctrl) { AddChild(ctrl, LayoutOp::OnRemaining, LayoutAlign::Full, -1, -1, -1); }
};

class WrapLayout : public LayoutBase<WrapLayout, Control> // requires a CanWrap panel
{
    struct Child {
        Control *Target;
        LONG Width;
    };

    HWND mParent;
    SIZE mMargin, mScaledMargin;
    vector<Child> mChildren;
    Layout *mLayout = nullptr;
    int mMinWidth = -1, mMaxWidth = -1;
    vector<UniquePtr<Layout>> mUsedLayouts;

    SIZE GetChildSize(Child &child, SizeType type) {
        if (child.Target && !child.Target->IsDisplayed()) {
            return {};
        }

        SIZE size = child.Target ? child.Target->GetSize(type) : SIZE{};
        if (child.Width >= 0) {
            size.cx = child.Width;
        }
        return size;
    }

    SIZE GetSize(SizeType type) override {
        if (type == SizeType::Ideal && mLayout) {
            return mLayout->GetSize(type);
        }

        SIZE size = {0, 0};
        for (auto &child : mChildren) {
            SIZE childSize = GetChildSize(child, type);
            size.cx = max(size.cx, childSize.cx);
            size.cy += childSize.cy + mScaledMargin.cy;
        }
        if (!mChildren.empty()) {
            size.cy -= mScaledMargin.cy;
        }
        return size;
    }

    Layout *NewLayout() {
        auto layout = UniquePtr<Layout>::New(mParent, 0, mMargin);
        Layout *ptr = layout;
        mUsedLayouts.push_back(move(layout));
        return ptr;
    }

    void OnSetRect(RECT rect, RectType type) override {
        int newWidth = RectWidth(rect);
        if (!mLayout || newWidth < mMinWidth || newWidth > mMaxWidth) {
            if (type == RectType::ForWrapping) {
                OnChildResize(RectType::ForWrapping);
            }

            size_t numPrevLayouts = mUsedLayouts.size();
            mMinWidth = 0;
            mMaxWidth = INT_MAX;
            mLayout = NewLayout();

            int width = 0;
            Layout *row = nullptr;
            for (auto &child : mChildren) {
                SIZE childSize = GetChildSize(child, SizeType::Ideal);
                int cmpWidth = width + childSize.cx;
                bool needWrap = cmpWidth > newWidth;
                if (needWrap) {
                    mMaxWidth = min(mMaxWidth, cmpWidth - 1);
                } else {
                    mMinWidth = max(mMinWidth, cmpWidth);
                }

                if (!row || needWrap) {
                    row = NewLayout();
                    mLayout->AddTop(row);
                    width = 0;
                }

                width += childSize.cx + mScaledMargin.cx;
                row->AddLeftMiddle(child.Target, child.Width);
            }

            mUsedLayouts.erase(mUsedLayouts.begin(), mUsedLayouts.begin() + numPrevLayouts);
            mLayout->SetParent(this);
        }

        return mLayout->OnSetRect(rect, type);
    }

    void OnChildResize(RectType type = RectType::Real) override {
        mLayout = nullptr;
        Control::OnChildResize(type);
    }

    void InvalidateSize() {
        if (mLayout) {
            OnChildResize();
        }
    }

    template <typename TOp>
    void ForEachChild(TOp &&op) {
        if (mLayout) {
            op(mLayout);
        } else {
            for (auto &child : mChildren) {
                if (child.Target) {
                    op(child.Target);
                }
            }
        }
    }

    friend class LayoutBase<WrapLayout, Control>;

public:
    WrapLayout(HWND parent, intptr_t, SIZE margin = {8, 8}) : mParent(parent), mMargin(margin), mScaledMargin(DpiScale(margin)) {}

    void Add(Control *ctrl, LONG width = -1) {
        mChildren.push_back(Child{ctrl, width});
        InvalidateSize();
    }

    void Clear() {
        mChildren.clear();
        InvalidateSize();
    }
};

class Overlay : public LayoutBase<Overlay, Control> {
    vector<Control *> mChildren;
    bool mNeedInvalidate = false;

    SIZE GetSize(SizeType type) override {
        mNeedInvalidate = true;
        SIZE size = {0, 0};
        for (auto &child : mChildren) {
            auto childSize = child->GetSize(type);
            size.cx = max(size.cx, childSize.cx);
            size.cy = max(size.cy, childSize.cy);
        }
        return size;
    }

    void OnSetRect(RECT rect, RectType type) override {
        for (auto &child : mChildren) {
            child->OnSetRect(rect, type);
        }
    }

    void OnChildResize(RectType type = RectType::Real) override {
        mNeedInvalidate = false;
        Control::OnChildResize(type);
    }

    void InvalidateSize() {
        if (mNeedInvalidate) {
            OnChildResize();
        }
    }

    template <typename TOp>
    void ForEachChild(TOp &&op) {
        for (auto &child : mChildren) {
            op(child);
        }
    }

    friend class LayoutBase<Overlay, Control>;

public:
    Overlay(HWND, intptr_t) {}

    void Add(Control *ctrl) {
        ctrl->SetParent(this);
        mChildren.push_back(ctrl);
        InvalidateSize();
    }

    void Clear() {
        mChildren.clear();
        InvalidateSize();
    }
};

class PopupMenu : public UiBase, public PopupIntf {
    HMENU mMenu = NULL;
    HWND mParent;
    function<void(PopupMenu *)> mOnCreate;

    void Add(ButtonBase *ctrl, int type, int state, bool isDef) {
        if (!ctrl->IsVisible()) {
            return;
        }

        int textLen;
        Path text = ctrl->GetText(&textLen);
        auto image = ctrl->GetImage();

        MENUITEMINFOW item = {};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_STRING | MIIM_ID | MIIM_FTYPE | MIIM_STATE;
        item.wID = ctrl->GetId();
        item.dwTypeData = text;
        item.cch = textLen;
        item.fType = MFT_STRING | type;
        item.fState = state;

        if (image) {
            item.fMask |= MIIM_BITMAP;
            item.hbmpItem = image->GetBitmap();
        }

        if (!ctrl->IsEnabled()) {
            item.fState |= MFS_DISABLED;
        }
        if (isDef) {
            item.fState |= MFS_DEFAULT;
        }

        InsertMenuItemW(mMenu, INT_MAX, TRUE, &item);
    }

public:
    PopupMenu(HWND parent, intptr_t id, function<void(PopupMenu *)> onCreate = {}) : mParent(parent), mOnCreate(move(onCreate)) {}

    virtual void OnCreate() {
        if (mOnCreate) {
            mOnCreate(this);
        }
    }
    virtual void OnDestroy() {}

    void Create(POINT pos) override {
        mMenu = CreatePopupMenu();
        OnCreate();

        TrackPopupMenu(mMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pos.x, pos.y, 0, mParent, nullptr);

        OnDestroy();
        DestroyMenu(mMenu);
    }

    void Create() { Create(GetOutput(GetCursorPos)); }

    void Add(Button *button, bool isDef = false) { Add(button, 0, 0, isDef); }
    void Add(CheckBase *check, bool isDef = false) {
        Add(check, 0, check->Get() ? MFS_CHECKED : MFS_UNCHECKED, isDef);
    }
    void Add(RadioBase *radio, bool isDef = false) {
        Add(radio, MFT_RADIOCHECK, radio->Get() ? MFS_CHECKED : MFS_UNCHECKED, isDef);
    }
    void Add(RadioGroupBase *group) {
        for (auto child : group->GetChildren()) {
            Add(child);
        }
    }
    void Add(SplitButton *split, bool isDef = false) { Add(split, 0, 0, isDef); }

    void AddSep() {
        MENUITEMINFOW item = {};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE;
        item.fType = MFT_SEPARATOR;
        InsertMenuItemW(mMenu, INT_MAX, TRUE, &item);
    }
};

class Shortcuts : public UiBase {
    HACCEL mAccel = nullptr;
    vector<ACCEL> mAccels;

    void Invalidate() {
        if (mAccel) {
            DestroyAcceleratorTable(mAccel);
            mAccel = nullptr;
        }
    }

public:
    Shortcuts(HWND, intptr_t) {}
    ~Shortcuts() { DestroyAcceleratorTable(mAccel); }

    void Add(HwndBase *dest, int key, int mods = 0) {
        ACCEL accel;
        accel.cmd = dest->GetId();
        accel.key = key;
        accel.fVirt = FVIRTKEY | mods;
        mAccels.push_back(accel);
        Invalidate();
    }

    bool Translate(HWND window, MSG *msg) {
        if (!mAccel && mAccels.size()) {
            mAccel = CreateAcceleratorTableW(mAccels.data(), (int)mAccels.size());
        }

        return mAccel ? TranslateAcceleratorW(window, mAccel, msg) != 0 : false;
    }
};

class PanelBase : public HwndBase {
protected:
    vector<UniquePtr<UiBase>> mChildren;
    Control *mRootChild = nullptr;
    Shortcuts *mShortcuts = nullptr;
    bool mClosed = false;

    static LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        PanelBase *self;
        if (msg == WM_NCCREATE) {
            self = (PanelBase *)((CREATESTRUCT *)lParam)->lpCreateParams;
            self->mControl = hwnd; // before CreateWindow returns
            self->SetUserData();
        } else {
            self = (PanelBase *)GetUserData(hwnd);
            if (!self || !self->mControl) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
        }

        return self->OnMessage(hwnd, msg, wParam, lParam);
    }

    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE:
            mClosed = false;
            ResetChildren(true);
            break;

        case WM_DESTROY:
            OnDestroy();
            mControl = nullptr;
            break;

        case WM_COMMAND:
            if (lParam) {
                WORD idx = LOWORD(wParam) - 1;
                if (idx < mChildren.size()) {
                    mChildren[idx]->OnCommand(HIWORD(wParam));
                }
            } else {
                WORD idx = LOWORD(wParam) - 1;
                if (idx < mChildren.size()) {
                    mChildren[idx]->OnIndirectCommand();
                }
            }
            break;

        case WM_NOTIFY: {
            auto nmhdr = (NMHDR *)lParam;
            UINT_PTR idx = nmhdr->idFrom - 1;
            if (idx < mChildren.size()) {
                LRESULT result = 0;
                mChildren[idx]->OnNotify(nmhdr, &result);
                return result;
            }
        } break;

        case WM_CTLCOLORSTATIC: {
            auto control = GetUserData((HWND)lParam);
            if (control) {
                control->OnCtlColor((HDC)wParam);
            }
        }
            return (LRESULT)GetStockObject(WHITE_BRUSH);

        case WM_VSCROLL:
        case WM_HSCROLL:
            if (lParam) {
                auto control = GetUserData((HWND)lParam);
                if (control) {
                    control->OnScroll(LOWORD(wParam));
                }
            }
            break;

        case WM_DRAWITEM: {
            WORD idx = LOWORD(wParam) - 1;
            if (idx < mChildren.size()) {
                mChildren[idx]->OnDrawItem((DRAWITEMSTRUCT *)lParam);
            }
        } break;

        case WM_DROPFILES: {
            auto hDrop = (HDROP)wParam;
            vector<Path> paths;
            int count = DragQueryFileW(hDrop, ~0u, nullptr, 0);
            for (int i = 0; i < count; i++) {
                size_t size = DragQueryFileW(hDrop, i, nullptr, 0) + 1;
                Path path{size};
                DragQueryFileW(hDrop, i, path, (UINT)size);
                paths.push_back(move(path));
            }
            OnFilesDrop(paths);
        }
            return 0;

        case WM_APP:
            (*(function<void()> *)wParam)();
            if (lParam) {
                delete (function<void()> *)wParam;
            }
            return 0;

        case WM_TIMER: {
            WORD idx = LOWORD(wParam) - 1;
            if (idx < mChildren.size()) {
                mChildren[idx]->OnCommand(0);
            }
        }
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static constexpr const wchar_t *WindowClassName = L"MyInputWndCls";

    void RegisterWindowClassIfNeeded() {
        static bool registered = false;
        if (registered) {
            return;
        }

        WNDCLASSW wincls = {};
        wincls.lpszClassName = WindowClassName;
        wincls.lpfnWndProc = WinProc;
        wincls.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wincls.hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCE(IDI_ICON));
        wincls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wincls);
        registered = true;
    }

    virtual bool IsTopLevel() { return false; }

    void ModalEventLoop() {
        MSG msg;
        while (!mClosed && GetMessageW(&msg, NULL, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                Control *ctrl = InitialFocus(); // hacky...
                if (ctrl && ctrl->OnKeyMsg(&msg)) {
                    continue;
                }
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void OnSetActive(bool active, void *root, void *prev) override {
        OnActivate(active, root, prev);
        if (mRootChild) {
            mRootChild->OnSetActive(active, root, prev);
        }
    }

    void Invalidate(bool now = false) {
        int flags = RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN;
        if (now) {
            flags |= RDW_ERASENOW | RDW_UPDATENOW;
        }
        RedrawWindow(mControl, nullptr, nullptr, flags);
    }

    bool OnKeyMsg(MSG *msg) override {
        if (mShortcuts && mShortcuts->Translate(mControl, msg)) {
            return true;
        }
        return Control::OnKeyMsg(msg);
    }

    void ResetChildren(bool fromCreate = false) {
        mChildren.clear();
        mRootChild = OnCreate();
        if (mRootChild) {
            mRootChild->SetParent(this);
        }
        if (!fromCreate) {
            OnChildResize();
        }
    }

public:
    // no ctor logic - need virtual functions!

    virtual Control *OnCreate() { return nullptr; }

    virtual void OnDestroy() {}
    virtual void OnActivate(bool activate, void *root, void *prev) {}
    virtual void OnFilesDrop(const vector<Path> &files) {}

    void EnableFilesDrop() {
        SetWindowLongW(mControl, GWL_EXSTYLE, GetWindowLongW(mControl, GWL_EXSTYLE) | WS_EX_ACCEPTFILES);
    }

    Shortcuts *GetShortcuts() {
        if (!mShortcuts) {
            mShortcuts = New<Shortcuts>();
        }
        return mShortcuts;
    }

    SIZE GetSize(SizeType type) override {
        if (mRootChild) {
            return mRootChild->GetSize(type);
        } else {
            return Control::GetSize(type);
        }
    }

    void Destroy() {
        if (mControl) {
            DestroyWindow(mControl); // will clear mControl from WM_DESTROY
        }
    }

    bool IsCreated() { return mControl != nullptr; }

    template <class TControl, class... TArgs>
    TControl *New(TArgs &&...args) {
        intptr_t id = mChildren.size() + 1;
        mChildren.emplace_back(UniquePtr<TControl>::New(mControl, id, forward<TArgs>(args)...));
        auto control = (TControl *)mChildren.back().get();
        control->OnNew();
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
protected:
    HWND mParent;

    void OnNew() {
        RegisterWindowClassIfNeeded();

        mControl = CreateWindowExW(WS_EX_CONTROLPARENT, WindowClassName, nullptr, WS_CHILD | WS_CLIPCHILDREN,
                                   0, 0, 0, 0, mParent, NULL, nullptr, this);
    }

    friend class PanelBase;

    void OnSetRect(RECT rect, RectType type) override {
        HwndBase::OnSetRect(rect, type);
        if (mRootChild) {
            rect.right -= rect.left;
            rect.bottom -= rect.top;
            rect.left = rect.top = 0;
            mRootChild->OnSetRect(rect, type);
        }
    }

    bool OnShow(bool show, HideType type) override {
        bool changed = HwndBase::OnShow(show, type);
        if (changed && mRootChild) {
            mRootChild->OnShow(show, HideType::FromParent);
        }
        return changed;
    }

    bool OnEnable(bool enable, HideType type) override {
        bool changed = HwndBase::OnEnable(enable, type);
        if (changed && mRootChild) {
            mRootChild->OnEnable(enable, HideType::FromParent);
        }
        return changed;
    }

public:
    Panel(HWND parent, intptr_t) : mParent(parent) {}
};

class ScrollPanel : public Panel {
    SIZE mFullSize = {-1, -1};
    SIZE mViewportSize = {};
    POINT mViewport = {};
    SIZE mLineSize = {-1, -1};
    SIZE mBarSize = {-1, -1};
    bool mPositioned = false;
    int mType;

    void OnNew() {
        RegisterWindowClassIfNeeded();

        mType = (int)GetScrollType();
        int flags = 0;
        if (mType != SB_HORZ) {
            flags |= WS_VSCROLL;
        }
        if (mType != SB_VERT) {
            flags |= WS_HSCROLL;
        }

        mControl = CreateWindowExW(WS_EX_CONTROLPARENT, WindowClassName, nullptr, WS_CHILD | WS_CLIPCHILDREN | flags,
                                   0, 0, 0, 0, mParent, NULL, nullptr, this);
    }

    friend class PanelBase;

    SIZE GetSize(SizeType type) override {
        SIZE maxSize = GetMaxSize();
        SIZE size = Control::GetSize(type);

        if (mType != SB_BOTH || maxSize.cx || maxSize.cy) {
            SIZE fullSize = Panel::GetSize(type);
            if (maxSize.cx) {
                size.cx = min(maxSize.cx, fullSize.cx);
            }
            if (maxSize.cy) {
                size.cy = min(maxSize.cy, fullSize.cy);
            }

            if (mType == SB_VERT) {
                size.cx = fullSize.cx;
            } else if (mType == SB_HORZ) {
                size.cy = fullSize.cy;
            }
        }
        return size;
    }

    int GetBarSize(int which) {
        if (mBarSize.cx < 0) {
            mBarSize.cx = GetSystemMetrics(SM_CXVSCROLL);
            mBarSize.cy = GetSystemMetrics(SM_CYHSCROLL);
        }
        return which == SB_VERT ? mBarSize.cx : mBarSize.cy;
    }
    int GetLineSize(int which) {
        if (mLineSize.cx < 0) {
            mLineSize = DialogUnitsToSize({18, 18});
        }
        return which == SB_HORZ ? mLineSize.cx : mLineSize.cy;
    }

    bool UpdateScroll(int which, int viewport, int size, LONG *pOffset, int cmd) {
        int maxEnd = size - 1;
        int maxPos = maxEnd - viewport;
        int oldPos = GetScrollPos(mControl, which);

        int pos = oldPos;
        switch (cmd) {
        case SB_TOP:
            pos = 0;
            break;
        case SB_BOTTOM:
            pos = maxPos;
            break;
        case SB_LINEDOWN:
            pos += GetLineSize(which);
            break;
        case SB_LINEUP:
            pos -= GetLineSize(which);
            break;
        case SB_PAGEDOWN:
            pos += viewport;
            break;
        case SB_PAGEUP:
            pos -= viewport;
            break;

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            SCROLLINFO trackInfo;
            trackInfo.cbSize = sizeof trackInfo;
            trackInfo.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(mControl, which, &trackInfo)) {
                pos = trackInfo.nTrackPos;
            }
        } break;
        }
        pos = Clamp(pos, 0, maxPos);

        SCROLLINFO info;
        info.cbSize = sizeof info;
        info.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
        info.nMin = 0;
        info.nMax = maxEnd;
        info.nPage = viewport;
        info.nPos = pos;

        SetScrollInfo(mControl, which, &info, cmd != SB_ENDSCROLL);
        *pOffset = pos;
        return pos != oldPos && viewport < size;
    }

    void UpdateScroll(int which, int cmd = SB_ENDSCROLL) {
        SIZE barSize = {};
        if (mViewportSize.cx < mFullSize.cx && mType != SB_VERT) {
            barSize.cy = GetBarSize(SB_HORZ);
        }
        if (mViewportSize.cy < mFullSize.cy && mType != SB_HORZ) {
            barSize.cx = GetBarSize(SB_VERT);
        }

        bool changed = false;
        if (which != SB_VERT && mType != SB_VERT) {
            changed |= UpdateScroll(SB_HORZ, mViewportSize.cx - barSize.cx, mFullSize.cx, &mViewport.x, cmd);
        }
        if (which != SB_HORZ && mType != SB_HORZ) {
            changed |= UpdateScroll(SB_VERT, mViewportSize.cy - barSize.cy, mFullSize.cy, &mViewport.y, cmd);
        }

        if ((changed || !mPositioned) && mRootChild) {
            POINT pos = {mViewport.x > 0 ? -mViewport.x : 0, mViewport.y > 0 ? -mViewport.y : 0};
            RECT rect = ToRect(pos, mFullSize);
            mRootChild->OnSetRect(rect);
            mPositioned = true;
            if (cmd != SB_ENDSCROLL) {
                Invalidate(cmd == SB_THUMBTRACK);
            }
        }
    }

    void OnSetRect(RECT rect, RectType type) override {
        HwndBase::OnSetRect(rect, type);

        bool needWrap = CanWrap() && mType == SB_VERT && mRootChild;

        auto newSize = RectSize(rect);
        if (needWrap && newSize.cx != mViewportSize.cx) {
            mFullSize = {-1, -1};
        }

        mViewportSize = newSize;

        if (mFullSize.cx < 0) {
            if (needWrap) {
                RECT fakeRect = ToRect(newSize);
                fakeRect.top = 0, fakeRect.bottom = INT_MAX;
                mRootChild->OnSetRect(fakeRect, RectType::ForWrapping);
            }

            mFullSize = Panel::GetSize(SizeType::Ideal);
        }

        UpdateScroll(SB_BOTH);
    }

    void OnChildResize(RectType type) override {
        mFullSize = {-1, -1};
        mPositioned = false;
        Control::OnChildResize(type);
    }

    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_VSCROLL:
            if (!lParam) {
                UpdateScroll(SB_VERT, LOWORD(wParam));
            }
            break;
        case WM_HSCROLL:
            if (!lParam) {
                UpdateScroll(SB_HORZ, LOWORD(wParam));
            }
            break;

        case WM_MOUSEWHEEL:
            if (mType == SB_HORZ) {
                break;
            }
            UpdateScroll(SB_VERT, (short)HIWORD(wParam) < 0 ? SB_LINEDOWN : SB_LINEUP);
            return 0;
        case WM_MOUSEHWHEEL:
            if (mType == SB_VERT) {
                break;
            }
            UpdateScroll(SB_HORZ, (short)HIWORD(wParam) > 0 ? SB_LINEDOWN : SB_LINEUP);
            return 0;
        }

        return Panel::OnMessage(hwnd, msg, wParam, lParam);
    }

protected:
    enum class ScrollType {
        Both = SB_BOTH,
        Horz = SB_HORZ,
        Vert = SB_VERT,
    };

    virtual ScrollType GetScrollType() { return ScrollType::Both; }
    virtual bool CanWrap() { return false; } // for ScrollType::Vert only
    virtual SIZE GetMaxSize() { return {}; }

public:
    using Panel::Panel;
};

class WindowBase : public PanelBase {
    bool mHadChildResize = false;
    HWND mPrevFocus = nullptr;

    virtual bool IsTopLevel() override { return true; }
    virtual int GetStyles() = 0;
    virtual int GetExStyles() = 0;

    bool MaxWithMinSize(SIZE *size) {
        SIZE minSize = mRootChild->GetSize(SizeType::Min);
        if (size->cx < minSize.cx || size->cy < minSize.cy) {
            size->cx = max(size->cx, minSize.cx);
            size->cy = max(size->cy, minSize.cy);
            return true;
        }
        return false;
    }

protected:
    RECT FromClientRect(RECT rect) {
        AdjustWindowRectEx(&rect, GetStyles(), false, GetExStyles());
        return rect;
    }
    RECT ToClientRect(RECT rect) {
        RECT empty = {};
        AdjustWindowRectEx(&empty, GetStyles(), false, GetExStyles());
        rect.left -= empty.left;
        rect.right -= empty.right;
        rect.top -= empty.top;
        rect.bottom -= empty.bottom;
        return rect;
    }

    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_CLOSE:
            if (OnClose()) {
                mClosed = true;
            }
            return 0;

        case WM_SIZE:
            if (IsTopLevel()) {
                if (mRootChild) {
                    mRootChild->OnSetRect(RECT{0, 0, LOWORD(lParam), HIWORD(lParam)});
                }
                Invalidate();
            }
            break;

        case WM_GETMINMAXINFO:
            if (mRootChild) {
                auto size = mRootChild->GetSize(SizeType::Min);
                size = RectSize(FromClientRect(ToRect(size)));
                ((MINMAXINFO *)lParam)->ptMinTrackSize = ToPoint(size);
                return 0;
            }
            break;

        case WM_ACTIVATE: {
            void *other = lParam ? GetUserData((HWND)lParam) : nullptr;
            bool activate = LOWORD(wParam) != WA_INACTIVE;
            OnSetActive(activate, nullptr, other);

            if (activate) {
                if (mPrevFocus && IsWindow(mPrevFocus)) {
                    ::SetFocus(mPrevFocus);
                } else {
                    Control *initFocus = InitialFocus();
                    if (initFocus) {
                        initFocus->SetFocus();
                    }
                }
            } else {
                mPrevFocus = GetFocus();
            }
        }
            return 0;
        }
        return PanelBase::OnMessage(hwnd, msg, wParam, lParam);
    }

    bool OnKeyMsg(MSG *msg) override {
        if (msg->wParam == VK_TAB && IsDialogMessageW(mControl, msg)) { // but not say VK_ENTER!
            return true;
        }

        return PanelBase::OnKeyMsg(msg);
    }

public:
    void Create(const wchar_t *title = L"", SIZE size = {-1, -1}, bool scale = true,
                POINT pos = {CW_USEDEFAULT, CW_USEDEFAULT}, POINT anchor = {-1, -1},
                HWND parent = nullptr, bool activate = true) {
        RegisterWindowClassIfNeeded();

        if (mControl) {
            Destroy();
        }

        SetRoot();
        if (scale) {
            size = DpiScale(size);
        }

        SIZE winSize = RectSize(FromClientRect(ToRect(size)));

        CreateWindowExW(WS_EX_CONTROLPARENT | GetExStyles(), WindowClassName, title, GetStyles() | WS_CLIPCHILDREN,
                        pos.x, pos.y, winSize.cx, winSize.cy,
                        parent, nullptr, nullptr, this);

        if (mRootChild) {
            if (size.cx < 0) {
                size = mRootChild->GetSize(SizeType::Ideal);
                SetSize(size, false);
            } else {
                if (MaxWithMinSize(&size)) {
                    SetSize(size, false);
                }
            }
        }

        if (pos.x != CW_USEDEFAULT && anchor.x >= 0) {
            pos.x -= anchor.x > 0 ? size.cx : size.cx / 2;
        }
        if (pos.y != CW_USEDEFAULT && anchor.y >= 0) {
            pos.y -= anchor.y > 0 ? size.cy : size.cy / 2;
        }

        if (anchor.x >= 0 || anchor.y >= 0) {
            SetPosition(pos);
        }

        ShowWindow(mControl, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    }

    void SetSize(SIZE size, bool scale = true) {
        if (scale) {
            size = DpiScale(size);
        }
        MaxWithMinSize(&size);

        size = RectSize(FromClientRect(ToRect(size)));

        RECT rect = GetOutput(GetWindowRect, mControl);
        rect.right = rect.left + size.cx;
        rect.bottom = rect.top + size.cy;
        OnSetRect(rect);
    }

    void SetPosition(POINT pos) {
        RECT rect = GetOutput(GetWindowRect, mControl);
        rect = {pos.x, pos.y, pos.x + RectWidth(rect), pos.y + RectHeight(rect)};
        OnSetRect(rect);
    }

    void SetRect(RECT rect) {
        RECT clientRect = ToClientRect(rect);

        SIZE size = RectSize(clientRect);
        if (MaxWithMinSize(&size)) {
            rect = FromClientRect(ToRect(RectPoint(clientRect), size));
        }

        OnSetRect(rect);
    }

    void OnChildResize(RectType type) override {
        if (!mHadChildResize && type == RectType::Real) {
            mHadChildResize = true;
            Post([this]() {
                RECT rect = GetOutput(GetClientRect, mControl);
                SIZE size = RectSize(rect);
                if (MaxWithMinSize(&size)) {
                    SetSize(size, false);
                } else {
                    if (mRootChild) {
                        mRootChild->OnSetRect(rect);
                    }
                    Invalidate();
                }

                mHadChildResize = false;
            });
        }
    }

    virtual bool OnClose() { return true; }
};

class Window : public WindowBase {
protected:
    virtual int GetStyles() override { return WS_OVERLAPPEDWINDOW; }
    virtual int GetExStyles() override { return WS_EX_OVERLAPPEDWINDOW; }

public:
    void Close() {
        SendMessageW(mControl, WM_CLOSE, 0, 0);
    }
};

class ModalWindow : public Window {
public:
    void Create(const wchar_t *title = L"", SIZE size = {-1, -1}, bool scale = true,
                POINT pos = {CW_USEDEFAULT, CW_USEDEFAULT}, POINT anchor = {-1, -1}) {
        HWND parent = GetActiveWindow();
        if (parent) {
            EnableWindow(parent, false);
        }

        if (parent && (pos.x == CW_USEDEFAULT || pos.y == CW_USEDEFAULT)) {
            RECT rect = GetOutput(GetWindowRect, parent);
            if (pos.x == CW_USEDEFAULT) {
                pos.x = RectCenterX(rect), anchor.x = 0;
            }
            if (pos.y == CW_USEDEFAULT) {
                pos.y = RectCenterY(rect), anchor.y = 0;
            }
        }

        WindowBase::Create(title, size, scale, pos, anchor, parent);
        ModalEventLoop();

        if (parent) {
            EnableWindow(parent, true);
        }

        Destroy();
    }
};

class ModalDialog : public ModalWindow {
    virtual int GetStyles() override { return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU; }
};

class PopupWindowBase : public WindowBase {
    virtual int GetStyles() override { return WS_POPUPWINDOW; }
    virtual int GetExStyles() override { return WS_EX_TOOLWINDOW; }

protected:
    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        if (msg == WM_ACTIVATE) {
            bool active = LOWORD(wParam) != WA_INACTIVE;
            if (active || (HWND)lParam != mParent) {
                SendMessageW(mParent, WM_NCACTIVATE, active, NULL);
            }
        }

        return WindowBase::OnMessage(hwnd, msg, wParam, lParam);
    }

    HWND mParent;

public:
    PopupWindowBase(HWND parent, intptr_t) {}

    virtual void Create(POINT pos, POINT anchor, SIZE size, bool scale) = 0;

    void Create(POINT pos, SIZE size = {-1, -1}) {
        Create(pos, {-1, -1}, size, true);
    }
    void Create(RECT rect) {
        RECT clientRect = ToClientRect(rect);
        Create(RectPoint(rect), {-1, -1}, RectSize(clientRect), false);
    }
    void Create(SIZE size = {-1, -1}, bool scale = true) {
        Create(GetOutput(GetCursorPos), {-1, -1}, size, scale);
    }
};

class PopupWindow : public PopupWindowBase, public PopupIntf {
    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
        if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE &&
            (!lParam || (HWND)lParam == mParent)) {
            mClosed = true;
            ShowWindow(mControl, SW_HIDE); // in case we get blocked before we close
        }

        return PopupWindowBase::OnMessage(hwnd, msg, wParam, lParam);
    }

public:
    void Create(POINT pos, POINT anchor, SIZE size, bool scale) override {
        mParent = GetActiveWindow();
        WindowBase::Create(L"", size, scale, pos, anchor, mParent);

        ModalEventLoop();
        Destroy();
    }

    void Create(POINT pos) override {
        PopupWindowBase::Create(pos);
    }

    using PopupWindowBase::Create;
    using PopupWindowBase::PopupWindowBase;
};

class OverlayWindow : public PopupWindowBase {
public:
    void Create(POINT pos, POINT anchor, SIZE size, bool scale) override {
        mParent = GetActiveWindow();
        WindowBase::Create(L"", size, scale, pos, anchor, mParent, false);
    }

    using PopupWindowBase::Create;
    using PopupWindowBase::PopupWindowBase;
};

class Clipboard {
    static void Set(int format, const void *data, size_t size) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();

            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
            if (mem) {
                memcpy(GlobalLock(mem), data, size);
                GlobalUnlock(mem);

                SetClipboardData(format, mem);
            }
            CloseClipboard();
        }
    }

    template <typename TCopy>
    static void Get(int format, TCopy &&copy) {
        if (OpenClipboard(nullptr)) {
            HGLOBAL mem = GetClipboardData(format);
            if (mem) {
                const void *data = GlobalLock(mem);
                if (data) {
                    copy(data, GlobalSize(mem));
                }
                GlobalUnlock(mem);
            }

            CloseClipboard();
        }
    }

public:
    static void SetText(const wchar_t *text) {
        size_t size = (wcslen(text) + 1) * sizeof(wchar_t);
        Set(CF_UNICODETEXT, text, size);
    }

    static Path GetText() {
        Path path;
        Get(CF_UNICODETEXT, [&](const void *dest, size_t size) {
            if (size < sizeof(wchar_t)) {
                return;
            }

            path = Path((const wchar_t *)dest, (size / 2) - 1);
        });
        return path;
    }
};

void UiInit() {
    HDC dc = GetDC(NULL);
    gDpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);

    INITCOMMONCONTROLSEX info = {};
    info.dwSize = sizeof(info);
    info.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&info);
}

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
