#pragma once
#include "UtilsUi.h"

StockIcon gStockIcon{SIID_SOFTWARE};

class TestWindow : public ModalWindow {
    class TestPopWindow : public PopupWindow {
    public:
        using PopupWindow::PopupWindow;

        Control *OnCreate() override {
            auto lv = New<ListView>([this](int i) {
                Alert(L"Clicked : %d", i);
                Destroy();
            });
            lv->CreateSingleColumn();
            lv->Add(L"Item 1");
            lv->Add(L"Item 2");
            lv->Add(L"Item 3");
            lv->Add(L"Item 4");

            auto lay = New<Layout>();
            lay->AddTop(New<CheckButton>(L"Toggleme", [](bool) { Alert(L"ok"); }));
            lay->AddTop(lv);
            return lay;
        }
    };

    class TestScrollPanel : public ScrollPanel {
    public:
        using ScrollPanel::ScrollPanel;

        Control *OnCreate() override {
            SIZE margin{32, 32};
            auto lay = New<Layout>(true, margin);
            for (int y = 0; y < 10; y++) {
                auto row = New<Layout>(false, margin);
                lay->AddTop(row);

                for (int x = 0; x < 10; x++) {
                    wchar_t text[1024];
                    swprintf(text, sizeof text, L"Hi %dx%d", x, y);
                    row->AddLeft(New<Button>(text, [x, y] { Alert(L"Hi %dx%d", x, y); }));
                }
            }
            return lay;
        }
    };

    class TestWrapPanel : public ScrollPanel {
    public:
        using ScrollPanel::ScrollPanel;

        ScrollType GetScrollType() { return ScrollType::Vert; }
        bool CanWrap() { return true; }

        Control *OnCreate() override {
            SIZE margin{32, 32};
            auto lay = New<Layout>(true, margin);
            for (int y = 0; y < 5; y++) {
                auto row = New<Layout>();
                lay->AddTop(row);
                row->AddLeftMiddle(New<Label>(L"Header:"));

                auto wrap = New<WrapLayout>(margin);
                row->AddRemaining(wrap);

                for (int x = 0; x < 15; x++) {
                    wchar_t text[1024];
                    swprintf(text, sizeof text, L"Hi %dx%d", x, y);
                    wrap->Add(New<Button>(text, [x, y] { Alert(L"Hi %dx%d", x, y); }));
                }
            }
            return lay;
        }
    };

    EditLine *mEdit = nullptr;
    EditBox *mEditBox = nullptr;
    DropDownEditLine *mDdEdit = nullptr;
    Control *mHideTarget = nullptr;
    PopupMenu *mPopMenu = nullptr;
    TestPopWindow *mPopWin = nullptr;
    MultiListView *mList = nullptr;
    ListBox *mBox = nullptr;
    TreeView *mTree = nullptr;
    ProgressBar *mProgress = nullptr;
    Timer *mTimer = nullptr;

    Control *OnCreate() override {
        auto layout = New<Layout>();
        auto toplay = New<Layout>();
        toplay->AddLeft(New<Label>(&gStockIcon));
        toplay->AddLeft(New<Label>(L"Hello World"));
        layout->AddTop(toplay);

        auto btnbar = New<Layout>();
        layout->AddTop(btnbar);
        auto btn = New<Button>(L"Click Me", &gStockIcon, []() { Alert(L"hello"); });
        btnbar->AddLeft(btn);
        auto chk = New<CheckBox>(L"Toggle Me", [](bool value) { Alert(L"state: %hs", value ? "set" : "unset"); });
        btnbar->AddLeft(chk);
        auto chk2 = New<CheckButton>(L"Hide Someone", [this](bool value) { mHideTarget->Display(!value); });
        btnbar->AddLeft(chk2);

        auto radbar = New<Layout>();
        layout->AddTop(mHideTarget = radbar);
        auto radgrp = New<RadioGroup<const char *>>([](const char *value) { Alert(L"state: %hs", value); });
        radbar->AddLeft(New<RadioBox>(L"First", radgrp, "first"));
        radbar->AddLeft(New<RadioBox>(L"Second", radgrp, "second"));
        radbar->AddLeft(New<RadioButton>(L"Third", radgrp, "third"));

        mPopWin = New<TestPopWindow>();
        mPopMenu = New<PopupMenu>([=](PopupMenu *pop) {
            pop->Add(btn);
            pop->Add(chk);
            pop->Add(chk2);
            pop->AddSep();
            pop->Add(radgrp);
        });

        layout->AddTopLeft(mEdit = New<EditLine>(L"..."));
        mEdit->SetOnFinish([this] { Alert(L"New value: %ws", mEdit->Get()); });
        layout->AddTopLeft(mEditBox = New<EditBox>(L"line1\r\nline2\r\n"), 40);
        mEditBox->SetOnFinish([this] { Alert(L"New value:\n%ws", mEditBox->Get()); });

        auto boxlay = New<Layout>();
        boxlay->AddLeft(mBox = New<ListBox>([this](int i) { Alert(L"sel: %d %s %p", i, mBox->Get(i), mBox->GetData(i)); }), Layout::Proportion(0.5));
        mBox->Add(L"Item 1", (void *)1);
        mBox->Add(L"Item 2", (void *)(uintptr_t)0x123456789abcdef0);
        mBox->Add(L"Item 3");

        MultiListBox *mbox;
        boxlay->AddRemaining(mbox = New<MultiListBox>([](const vector<int> &is) { Alert(L"sel count:% d", is.size()); }));
        mbox->Add(L"Item 1");
        mbox->Add(L"Item 2");
        mbox->Add(L"Item 3");
        layout->AddTop(boxlay);

        DropDownList *ddl;
        layout->AddTopLeft(ddl = New<DropDownList>([](int i) { Alert(L"drop: %d", i); }));
        ddl->Add(L"Default");
        ddl->Add(L"Other");
        ddl->Add(L"Moreso");
        ddl->SetSelected(0, false);

        layout->AddTopLeft(New<UpDownButtons>([](bool b) { Alert(L"updown: %d", b); }));

        layout->AddTopLeft(mDdEdit = New<DropDownEditLine>(L"..."));
        mDdEdit->Add(L"Default");
        mDdEdit->Add(L"Other");
        mDdEdit->Add(L"Moreso");
        mDdEdit->SetOnFinish([this]() { Alert(L"droped: %ws", mDdEdit->Get()); });

        auto layout2 = New<Layout>();
        layout2->AddTopMiddle(New<Label>(L"Hello World 2"));
        layout2->AddTopLeft(New<SplitButton>(L"Click Me 2", mPopMenu, []() { Alert(L"hello 2"); }));
        layout2->AddTopLeft(New<SplitButton>(L"Click Me 3", mPopWin, []() { Alert(L"hello 3"); }));

        Slider *slider = New<Slider>(100);
        slider->SetOnFinish([](int pos) { Alert(L"Slider %d", pos); });
        layout2->AddTopLeft(slider);

        auto pcbox = New<PartialCheckBox>(L"Click Me For?", ToggleDown, [](MaybeBool state) { Alert(L"3state: %d", (int)state); });
        layout2->AddTopLeft(pcbox);

        auto editint = New<EditInt<int8_t>>(99, [](int8_t value) { Alert(L"num: %d", value); });
        editint->SetRange(-100, 100);
        layout2->AddTopLeft(editint);

        mProgress = New<ProgressBar>(1000);
        mTimer = New<Timer>(0.1, [this] { mProgress->Add(); });
        layout2->AddTop(mProgress);

        layout2->AddTop(mList = New<MultiListView>(), Layout::Proportion(0.5));
        mList->SetOnCheck([this](int i, bool b) { Alert(L"item %d (%p) check: %d", i, mList->GetData(i), b); });
        mList->SetOnClick([this](int i, int c, bool right, bool) {
            if (c == 0 && right) {
                mPopMenu->Create();
            } else if (c == 1 && right) {
                mPopWin->Create();
            }
        });
        mList->AddColumn(L"Text", 100);
        mList->AddColumn(L"Col2", 50);
        mList->Add(L"Item 1", (void *)1);
        mList->Add(L"Item 2", (void *)(uintptr_t)0x123456789abcdef0);
        mList->Add(L"Item 3");
        mList->Add(L"Item 4");
        mList->Set(0, 1, L"Col2Val");
        mList->Set(3, 1, L"Col2Val2");
        mList->SetChecked(0, true, false);

        layout2->AddRemaining(mTree = New<TreeView>([this](TreeNode n) { Alert(L"tree sel: %p", mTree->GetData(n)); }));
        mTree->SetOnCheck([this](TreeNode n, bool b) { Alert(L"tree check: %p = %d", mTree->GetData(n), b); });
        mTree->SetOnClick([this](TreeNode n, bool right, bool) {
            if (right) {
                mPopMenu->Create(); // dup.
            }
        });
        TreeNode tr1 = mTree->Add(nullptr, L"First Root", (void *)1);
        TreeNode tr2 = mTree->Add(nullptr, L"Second Root", (void *)2);
        mTree->Add(tr1, L"Item 1");
        mTree->Add(tr1, L"Item 2", (void *)(uintptr_t)0x123456789abcdef0);
        mTree->Add(mTree->Add(tr1, L"Item 3"), L"Item 3.2");
        mTree->Add(tr2, L"Second Item 1", (void *)0x11);
        mTree->Add(tr2, L"Second Item 2", (void *)0x12);
        mTree->SetChecked(tr1, true, false);
        mTree->SetExpanded(tr1);

        auto layout3 = New<Layout>();
        layout3->AddTop(New<TestScrollPanel>(), Layout::Proportion(0.5));
        layout3->AddTop(New<Separator>());
        layout3->AddRemaining(New<TestWrapPanel>());

        auto tab = New<UnionTab>();
        tab->Add(L"Hello", layout);
        tab->Add(L"Hello2", layout2);
        tab->Add(L"Hello3", layout3);
        return tab;
    }

public:
    void Create() {
        ModalWindow::Create(L"UiTest", SIZE{600, 500});
    }
};

void DoUiTest() {
    UiInit();
    TestWindow window;
    window.Create();
}
