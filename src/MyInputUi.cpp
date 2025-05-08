#include "UtilsUi.h"
#include "Registry.h"
#include "ExeUi.h"
#include "ConfigUi.h"
#include "ConfigTestUi.h"
#include <Windows.h>

DEFINE_ALERT_ON_ERROR()

class MyInputWindow : public ModalWindow, public BaseUiIntf {
    ExePanel *mExePanel = nullptr;
    ConfigPanel *mConfigPanel = nullptr;
    ConfigTestPanel *mTestPanel = nullptr;
    UnionTab *mTab = nullptr;

    Control *OnCreate() override {
        mExePanel = New<ExePanel>(this);
        mConfigPanel = New<ConfigPanel>(mExePanel);
        mTestPanel = New<ConfigTestPanel>(mConfigPanel);

        mTab = New<UnionTab>();
        mTab->Add(L"Executables", mExePanel);
        mTab->Add(L"Configs", mConfigPanel);
        mTab->Add(L"Test", mTestPanel);
        return mTab;
    }

    Control *InitialFocus() override { return mTab->InitialFocus(); }

public:
    void Create() {
        ModalWindow::Create(L"MyInput", SIZE{900, 600});
    }

    void SwitchToConfigs() override {
        mTab->SetSelected(mConfigPanel);
    }
};

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    UiInit();
    MyInputWindow window;
    window.Create();
    return 0;
}
