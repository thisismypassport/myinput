#pragma once
#include "UtilsUiBase.h"
#include <CommCtrl.h>

class IPositioned {
public:
    virtual void SetPosition(RECT rect) = 0;
};

class Control : public IPositioned {
protected:
    HWND mControl = nullptr;

    virtual void OnCommand(WORD value) {}

    void InitFont() {
        SendMessageW(mControl, WM_SETFONT, (LPARAM)GetStockObject(DEFAULT_GUI_FONT), false);
    }

    friend class Window;

public:
    void Show(bool show) { ShowWindow(mControl, show ? SW_SHOWNOACTIVATE : SW_HIDE); }
    void Hide() { Show(false); }
    void Enable(bool enable) { EnableWindow(mControl, enable); }
    void Disable() { Enable(false); }
    void Focus() { SetFocus(mControl); }

    void SetPosition(RECT rect) {
        MoveWindow(mControl, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, false);
    }
};

class Button : public Control {
    function<void()> mAction;

public:
    Button(HWND window, intptr_t id, const wchar_t *text, function<void()> &&action) {
        mAction = move(action);
        mControl = CreateWindowW(WC_BUTTONW, text, WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    void OnCommand(WORD value) override {
        mAction();
    }
};

class CheckBox : public Control {
    function<void(bool value)> mAction;

public:
    CheckBox(HWND window, intptr_t id, const wchar_t *text, function<void(bool)> &&action) {
        mAction = move(action);
        mControl = CreateWindowW(WC_BUTTONW, text, WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    bool Get() { return SendMessageW(mControl, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void Set(bool value = true) { SendMessageW(mControl, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
    void Unset() { Set(false); }

    void OnCommand(WORD value) override {
        mAction(Get());
    }
};

class Label : public Control {
public:
    Label(HWND window, intptr_t id, const wchar_t *text) {
        mControl = CreateWindowW(WC_STATICW, text, WS_CHILD | WS_VISIBLE,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
        InitFont();
    }
};

class EditBase : public Control {
public:
    Path Get() {
        int size = GetWindowTextLengthW(mControl) + 1;

        Path text(size);
        if (!GetWindowTextW(mControl, text, size)) {
            text[0] = L'\0';
        }
        return text;
    }

    void Set(const wchar_t *value) {
        SetWindowTextW(mControl, value);
    }
};

class EditLine : public EditBase {
    function<void()> mChanged;

public:
    EditLine(HWND window, intptr_t id, function<void()> &&changed) {
        mChanged = move(changed);
        mControl = CreateWindowW(WC_EDITW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VISIBLE | WS_BORDER |
                                     ES_AUTOHSCROLL | ES_LEFT | ES_NOHIDESEL,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    void OnCommand(WORD value) override {
        if (value == EN_CHANGE) {
            mChanged();
        }
    }
};

class ListBoxBase : public Control {
public:
    int Add(const wchar_t *str) { return (int)SendMessageW(mControl, LB_ADDSTRING, 0, (LPARAM)str); }
    void Insert(int idx, const wchar_t *str) { SendMessageW(mControl, LB_INSERTSTRING, idx, (LPARAM)str); }

    template <class T>
    int Add(const wchar_t *str, T data) {
        int idx = Add(str);
        SetData<T>(idx, data);
        return idx;
    }

    void Remove(int idx) { SendMessageW(mControl, LB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, LB_RESETCONTENT, 0, 0); }

    template <class T>
    void SetData(int idx, T data) { SendMessageW(mControl, LB_SETITEMDATA, idx, (LPARAM)data); }

    template <class T>
    T GetData(int idx) { return idx >= 0 ? (T)SendMessage(mControl, LB_GETITEMDATA, idx, 0) : (T)0; }
};

class ListBox : public ListBoxBase {
    function<void(int)> mAction;

public:
    ListBox(HWND window, intptr_t id, function<void(int idx)> &&action) {
        mAction = move(action);
        mControl = CreateWindowW(WC_LISTBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_VISIBLE | WS_BORDER |
                                     LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
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
    MultiListBox(HWND window, intptr_t id, function<void(const vector<int> &indices)> &&action) {
        mAction = move(action);
        mControl = CreateWindowW(WC_LISTBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VSCROLL | WS_VISIBLE | WS_BORDER |
                                     LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
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

class ListDropDown : public Control {
    function<void(int)> mAction;

public:
    ListDropDown(HWND window, intptr_t id, function<void(int idx)> &&action) {
        mAction = move(action);
        mControl = CreateWindowW(WC_COMBOBOXW, nullptr,
                                 WS_TABSTOP | WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                                 0, 0, 0, 0, window, (HMENU)id, nullptr, nullptr);
        InitFont();
    }

    int Add(const wchar_t *str) { return (int)SendMessageW(mControl, CB_ADDSTRING, 0, (LPARAM)str); }
    void Add(int idx, const wchar_t *str) { SendMessageW(mControl, CB_INSERTSTRING, idx, (LPARAM)str); }

    template <class T>
    int Add(const wchar_t *str, T data) {
        int idx = Add(str);
        SetData<T>(idx, data);
        return idx;
    }

    void Remove(int idx) { SendMessageW(mControl, CB_DELETESTRING, idx, 0); }
    void Clear() { SendMessageW(mControl, CB_RESETCONTENT, 0, 0); }

    template <class T>
    void SetData(int idx, T data) { SendMessageW(mControl, CB_SETITEMDATA, idx, (LPARAM)data); }

    template <class T>
    T GetData(int idx) { return idx >= 0 ? (T)SendMessage(mControl, CB_GETITEMDATA, idx, 0) : (T)0; }

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
};

class Layout : public IPositioned {
    RECT mRect;
    SIZE mMargin;

public:
    Layout(SIZE size, SIZE margin, bool outerMargins = true) : mRect{0, 0, size.cx, size.cy}, mMargin(margin) {
        if (outerMargins) {
            mRect.left += mMargin.cx;
            mRect.top += mMargin.cy;
            mRect.right -= mMargin.cx;
            mRect.bottom -= mMargin.cy;
            mRect.right = max(mRect.right, mRect.left);
            mRect.bottom = max(mRect.bottom, mRect.top);
        }
    }

    void SetPosition(RECT rect) override {
        mRect = rect;
    }

    Layout SubLayout() {
        return Layout({0, 0}, mMargin, false);
    }

    void OnTop(IPositioned *ctrl, LONG height, LONG offset = 0, LONG margin = -1) {
        height = min(height, mRect.bottom - mRect.top);
        ctrl->SetPosition(RECT{mRect.left + offset, mRect.top, mRect.right - offset, mRect.top + height});
        mRect.top += height + (margin >= 0 ? margin : mMargin.cy);
        mRect.top = min(mRect.top, mRect.bottom);
    }

    void OnBottom(IPositioned *ctrl, LONG height, LONG offset = 0, LONG margin = -1) {
        height = min(height, mRect.bottom - mRect.top);
        ctrl->SetPosition(RECT{mRect.left + offset, mRect.bottom - height, mRect.right - offset, mRect.bottom});
        mRect.bottom -= height + (margin >= 0 ? margin : mMargin.cy);
        mRect.bottom = max(mRect.bottom, mRect.top);
    }

    void OnLeft(IPositioned *ctrl, LONG width, LONG offset = 0, LONG margin = -1) {
        width = min(width, mRect.right - mRect.left);
        ctrl->SetPosition(RECT{mRect.left, mRect.top + offset, mRect.left + width, mRect.bottom - offset});
        mRect.left += width + (margin >= 0 ? margin : mMargin.cx);
        mRect.left = min(mRect.left, mRect.right);
    }

    void OnRight(IPositioned *ctrl, LONG width, LONG offset = 0, LONG margin = -1) {
        width = min(width, mRect.right - mRect.left);
        ctrl->SetPosition(RECT{mRect.right - width, mRect.top + offset, mRect.right, mRect.bottom - offset});
        mRect.right -= width + (margin >= 0 ? margin : mMargin.cx);
        mRect.right = max(mRect.right, mRect.left);
    }

    void OnRest(IPositioned *ctrl) {
        ctrl->SetPosition(RECT{mRect.left, mRect.top, mRect.right, mRect.bottom});
        mRect.right = mRect.left;
        mRect.bottom = mRect.top;
    }
};

class Window {
    HWND mWindow = nullptr;
    vector<UniquePtr<Control>> mControls;

    static LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Window *self;
        if (msg == WM_NCCREATE) {
            self = (Window *)((CREATESTRUCT *)lParam)->lpCreateParams;
            self->mWindow = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = (Window *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        switch (msg) {
        case WM_CREATE:
            self->OnCreate();
            break;
        case WM_DESTROY:
            self->OnDestroy();
            break;

        case WM_SIZE:
            self->OnResize(SIZE{LOWORD(lParam), HIWORD(lParam)});
            InvalidateRect(self->mWindow, nullptr, true);
            break;

        case WM_COMMAND:
            if (lParam) {
                WORD idx = LOWORD(wParam) - 1;
                if (idx < self->mControls.size()) {
                    self->mControls[idx]->OnCommand(HIWORD(wParam));
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

public:
    // no ctor/dtor - need virtual functions!

    void Create() {
        WNDCLASSW wincls = {};
        wincls.lpszClassName = ClassName();
        wincls.lpfnWndProc = WinProc;
        wincls.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wincls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wincls);

        SIZE size = InitialSize();
        RECT rect = {0, 0, size.cx, size.cy};
        int style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        int xstyle = WS_EX_OVERLAPPEDWINDOW;
        AdjustWindowRectEx(&rect, style, false, xstyle);

        POINT pos = InitialPos();
        CreateWindowExW(xstyle, wincls.lpszClassName, InitialTitle(),
                        style, pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top,
                        nullptr, nullptr, nullptr, this);
    }

    virtual const wchar_t *ClassName() { return L"Top-Level Window"; }
    virtual const wchar_t *InitialTitle() { return L""; }
    virtual POINT InitialPos() { return POINT{CW_USEDEFAULT, CW_USEDEFAULT}; }
    virtual SIZE InitialSize() { return SIZE{100, 100}; }

    virtual void OnCreate() {}
    virtual void OnDestroy() { PostQuitMessage(0); }
    virtual void OnResize(SIZE size) {}

    template <class TControl, class... TArgs>
    TControl *Add(TArgs &&...args) {
        intptr_t id = mControls.size() + 1;
        mControls.emplace_back(UniquePtr<TControl>::New(mWindow, id, forward<TArgs>(args)...));
        return (TControl *)mControls.back().get();
    }

    void Post(function<void()> &&func) {
        PostMessageW(mWindow, WM_APP, (WPARAM) new function<void()>(move(func)), true);
    }

    void Call(const function<void()> &func) {
        SendMessageW(mWindow, WM_APP, (WPARAM)&func, false);
    }
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
