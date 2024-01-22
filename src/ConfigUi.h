#pragma once

// TODO, maybe

class ConfigPanel : public Panel {
public:
    using Panel::Panel;

    IControl *OnCreate() override {
        return Add<Label>(L"Testing...");
    }
};
