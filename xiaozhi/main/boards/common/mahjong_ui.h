#pragma once

class Display;

// 800x480 麻将牌局助手界面（LVGL）。
// 由 4.3C 板在显示初始化完成后调用。
class MahjongUi {
public:
    static void Init(Display* display);
    static void Toggle();
};
