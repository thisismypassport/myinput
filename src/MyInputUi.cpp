#include "UtilsUi.h"
#include "Registry.h"
#include "ExeUi.h"
#include "ConfigUi.h"
#include "LogUi.h"
#include <Windows.h>

DEFINE_ALERT_ON_LOG(LogLevel::Error)

class MyInputWindow : public MainWindow {
    const wchar_t *InitialTitle() override { return L"MyInput"; }
    SIZE InitialSize() override { return SIZE{1000, 600}; }

    IControl *OnCreate() override {
        auto exes = Add<ExePanel>();
        auto configs = Add<ConfigPanel>();
        auto logs = Add<LogPanel>();

        return exes;
    }
};

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    MyInputWindow window;
    window.Create();
    ProcessWindows();
}
