#pragma once

// TODO, maybe

class LogPanel : public Panel {
public:
    using Panel::Panel;

    IControl *OnCreate() override {
        EditLine *line = Add<EditLine>([this](bool) {
            // ...
        });

        line->Set(L"Testing...");

        return line;
    }
};
