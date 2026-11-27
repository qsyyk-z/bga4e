#include "mahjong_ui.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "display.h"
#include "camera_cards.h"
#include "local_game.h"

#define TAG "MahjongUi"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_puhui_30_4);

namespace {

constexpr uint32_t COLOR_BG = 0xF4ECDD;
constexpr uint32_t COLOR_PANEL = 0xFFFAF0;
constexpr uint32_t COLOR_INK = 0x25221D;
constexpr uint32_t COLOR_MUTED = 0x6F675B;
constexpr uint32_t COLOR_TABLE = 0x1D5A47;
constexpr uint32_t COLOR_GOLD = 0xC49A2C;
constexpr uint32_t COLOR_RED = 0xB23A2B;

lv_obj_t* MakePanel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xD8CDB9), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t* MakeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                    uint32_t color, int x, int y, int width = 0) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    if (width > 0) {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    return label;
}

lv_obj_t* MakeButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                     lv_event_cb_t cb, void* user_data) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_TABLE), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

lv_obj_t* MakeTile(lv_obj_t* parent, const std::string& name, bool poker) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_size(tile, poker ? 54 : 44, 56);
    lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xCDBFA7), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_pad_all(tile, 2, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, name.c_str());
    lv_obj_set_style_text_font(label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_INK), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return tile;
}

lv_obj_t* MakeHandTile(lv_obj_t* parent, const std::string& name, bool poker) {
    lv_obj_t* tile = MakeTile(parent, name, poker);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    // 让点在小字上的触摸也冒泡到牌块，统一由牌块处理出牌。
    lv_obj_add_flag(lv_obj_get_child(tile, 0), LV_OBJ_FLAG_EVENT_BUBBLE);
    return tile;
}

lv_obj_t* MakeMiniTile(lv_obj_t* parent, const std::string& name, int w, int h,
                       bool clickable) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xCDBFA7), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 4, 0);
    lv_obj_set_style_pad_all(tile, 1, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, name.c_str());
    lv_obj_set_style_text_font(label, w >= 34 ? &font_puhui_16_4 : &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_INK), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    if (clickable) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return tile;
}

void StyleScreen(lv_obj_t* screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

class MahjongUiImpl {
public:
    explicit MahjongUiImpl(Display* display) : display_(display) {}

    void Init() {
        DisplayLockGuard lock(display_);
        chat_screen_ = lv_screen_active();
        CreateMenu();
        CreateSetup();
        CreateReady();
        CreateGame();
        CreateChatReturnButton();
        LoadScreen(menu_screen_);
        ESP_LOGI(TAG, "init active screen: %s", ScreenName(lv_screen_active()));

        // 有些情况下 LVGL 初始化末尾会把默认屏幕重新置顶，启动后
        // 再补一次，确保用户第一眼看到的是牌局菜单而不是小智聊天页。
        lv_timer_t* boot_timer = lv_timer_create([](lv_timer_t* timer) {
            auto* self = static_cast<MahjongUiImpl*>(lv_timer_get_user_data(timer));
            if (lv_screen_active() == self->chat_screen_ &&
                !LocalGame::Instance().active()) {
                self->LoadScreen(self->menu_screen_);
                ESP_LOGW(TAG, "boot screen was chat; menu restored");
            }
            lv_timer_delete(timer);
        }, 700, this);
        lv_timer_set_repeat_count(boot_timer, 1);
    }

    void StartTask() {
        xTaskCreate([](void* arg) {
            static_cast<MahjongUiImpl*>(arg)->TaskLoop();
        }, "mahjong_ui", 8192, this, 3, &task_);
    }

    void Toggle() {
        DisplayLockGuard lock(display_);
        if (lv_screen_active() == chat_screen_) {
            if (LocalGame::Instance().active()) {
                LoadScreen(game_screen_);
                UpdateGameLocked();
            } else {
                LoadScreen(menu_screen_);
            }
        } else {
            LoadScreen(chat_screen_);
        }
    }

private:
    const char* ScreenName(lv_obj_t* screen) const {
        if (screen == chat_screen_) return "chat";
        if (screen == menu_screen_) return "menu";
        if (screen == setup_screen_) return "setup";
        if (screen == ready_screen_) return "ready";
        if (screen == game_screen_) return "game";
        return "unknown";
    }

    void LoadScreen(lv_obj_t* screen) {
        lv_screen_load(screen);
        // 关键：切换屏幕时吞掉当前这次触摸的抬起事件。
        // 否则手指还停在原坐标，新屏幕同位置的按钮会立刻收到一次点击，
        // 表现为“刚进入主界面又马上跳回聊天页”。
        lv_indev_t* indev = lv_indev_active();
        if (indev != nullptr) {
            lv_indev_wait_release(indev);
        }
        ESP_LOGI(TAG, "screen -> %s", ScreenName(screen));
    }

    void CreateMenu() {
        menu_screen_ = lv_obj_create(NULL);
        StyleScreen(menu_screen_);

        lv_obj_t* top = lv_obj_create(menu_screen_);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 800, 92);
        lv_obj_set_style_bg_color(top, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(top, 0, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
        MakeLabel(top, "BGA4E 牌局助手", &font_puhui_30_4, COLOR_GOLD, 24, 14);
        MakeLabel(top, "离线版 · 不依赖后端 / WiFi", &font_puhui_20_4, 0xD7E6DC, 24, 52);

        MakeButton(menu_screen_, "开始牌局", 76, 150, 300, 116, OnOpenSetup, this);
        MakeButton(menu_screen_, "生成演示牌局", 424, 150, 300, 116, OnDemo, this);

        menu_status_ = MakeLabel(menu_screen_, "选择一种方式开始。\n演示模式会自动生成手牌并自动摸打。",
                                 &font_puhui_20_4, COLOR_INK, 76, 306, 650);
        MakeLabel(menu_screen_, "提示：点右侧“语音”可以和小智说话。\n"
                                "4.3C 的 BOOT 键与屏幕数据脚复用，不能当功能键。",
                  &font_puhui_20_4, COLOR_MUTED, 76, 378, 650);
        // 不要放在 (680,22)：聊天页的“牌局”按钮在 (700,36,84,36)，
        // 两块触摸区域重叠会在切屏瞬间被同一次抬手误触。
        MakeButton(menu_screen_, "语音", 680, 100, 90, 44, OnChat, this);
    }

    void CreateSetup() {
        setup_screen_ = lv_obj_create(NULL);
        StyleScreen(setup_screen_);

        lv_obj_t* top = lv_obj_create(setup_screen_);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 800, 70);
        lv_obj_set_style_bg_color(top, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(top, 0, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
        MakeLabel(top, "开始牌局", &font_puhui_30_4, COLOR_GOLD, 24, 14);
        MakeButton(top, "语音", 576, 14, 84, 42, OnChat, this);
        MakeButton(top, "返回", 680, 14, 90, 42, OnMenu, this);

        MakeLabel(setup_screen_, "游戏类型", &font_puhui_20_4, COLOR_MUTED, 80, 104);
        type_buttons_[0] = MakeButton(setup_screen_, "麻将", 80, 140, 190, 64,
                                      OnTypeMahjong, this);
        type_buttons_[1] = MakeButton(setup_screen_, "扑克", 290, 140, 190, 64,
                                      OnTypePoker, this);

        MakeLabel(setup_screen_, "你的座位", &font_puhui_20_4, COLOR_MUTED, 80, 236);
        seat_buttons_[0] = MakeButton(setup_screen_, "东", 80, 272, 140, 60,
                                      OnSeatEast, this);
        seat_buttons_[1] = MakeButton(setup_screen_, "南", 238, 272, 140, 60,
                                      OnSeatSouth, this);
        seat_buttons_[2] = MakeButton(setup_screen_, "西", 396, 272, 140, 60,
                                      OnSeatWest, this);
        seat_buttons_[3] = MakeButton(setup_screen_, "北", 554, 272, 140, 60,
                                      OnSeatNorth, this);

        setup_hint_ = MakeLabel(setup_screen_, "", &font_puhui_20_4, COLOR_INK,
                                80, 356, 640);
        MakeButton(setup_screen_, "下一步", 560, 396, 180, 56, OnSetupNext, this);
        UpdateSetupLocked();
    }

    void CreateReady() {
        ready_screen_ = lv_obj_create(NULL);
        StyleScreen(ready_screen_);

        lv_obj_t* top = lv_obj_create(ready_screen_);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 800, 70);
        lv_obj_set_style_bg_color(top, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(top, 0, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
        MakeLabel(top, "读入手牌", &font_puhui_30_4, COLOR_GOLD, 24, 14);
        MakeButton(top, "语音", 580, 14, 84, 42, OnChat, this);
        MakeButton(top, "返回", 680, 14, 90, 42, OnBackSetup, this);

        ready_hint_ = MakeLabel(ready_screen_,
                                "请通过摄像头读入手牌；\n"
                                "把摄像头 JSON 放到 TF 卡 bga4e/cards.json，点“SD读牌”；\n"
                                "也可以点“语音”对小智报牌，例如：\n"
                                "“我手里有一万、二万、三条……”",
                                &font_puhui_20_4, COLOR_INK, 70, 116, 660);
        MakeButton(ready_screen_, "读入完成", 220, 336, 190, 64, OnHandReady, this);
        MakeButton(ready_screen_, "SD读牌", 420, 336, 160, 64, OnSdRead, this);
        MakeButton(ready_screen_, "返回设置", 590, 336, 160, 64, OnBackSetup, this);
    }

    void CreateGame() {
        game_screen_ = lv_obj_create(NULL);
        StyleScreen(game_screen_);

        lv_obj_t* top = lv_obj_create(game_screen_);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 800, 58);
        lv_obj_set_style_bg_color(top, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(top, 0, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
        game_title_ = MakeLabel(top, "离线牌局", &font_puhui_30_4, COLOR_GOLD, 18, 10);
        game_status_ = MakeLabel(top, "准备中", &font_puhui_20_4, 0xD7E6DC, 300, 18, 190);

        MakeButton(top, "建议", 500, 11, 66, 36, OnAdvice, this);
        MakeButton(top, "理牌", 570, 11, 66, 36, OnSort, this);
        MakeButton(top, "语音", 640, 11, 66, 36, OnChat, this);
        MakeButton(top, "菜单", 710, 11, 80, 36, OnMenu, this);

        lv_obj_t* info = MakePanel(game_screen_, 12, 64, 776, 40);
        turn_label_ = MakeLabel(info, "当前：— · 牌墙 —", &font_puhui_20_4,
                                COLOR_INK, 12, 2, 750);
        hint_label_ = MakeLabel(info, "", &font_puhui_14_1, COLOR_MUTED, 12, 22, 750);

        // 牌桌背景。四个玩家区按“自己永远在下家”旋转：下=自己，右=下家，上=对家，左=上家。
        lv_obj_t* table = lv_obj_create(game_screen_);
        lv_obj_set_pos(table, 12, 108);
        lv_obj_set_size(table, 776, 304);
        lv_obj_set_style_bg_color(table, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(table, 18, 0);
        lv_obj_set_style_border_width(table, 0, 0);
        lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

        CreateZone(0, 150, 110, 500, 100, false, false);  // 对家
        CreateZone(1, 656, 110, 132, 300, true, false);   // 下家（右）
        CreateZone(2, 150, 304, 500, 106, false, true);   // 自己（下）
        CreateZone(3, 12, 110, 132, 300, true, false);    // 上家（左）

        lv_obj_t* center = MakePanel(game_screen_, 150, 214, 500, 84);
        last_label_ = MakeLabel(center, "最近出牌：—", &font_puhui_16_4, COLOR_RED, 10, 2, 480);
        advice_action_ = MakeLabel(center, "等待牌局…", &font_puhui_20_4, COLOR_TABLE,
                                   10, 20, 480);
        advice_reason_ = MakeLabel(center, "点击“建议”获取本地建议。",
                                   &font_puhui_14_1, COLOR_INK, 10, 42, 480);
        advice_meta_ = MakeLabel(center, "", &font_puhui_14_1, COLOR_MUTED, 10, 62, 480);
        chi_btn_ = MakeButton(center, "吃", 8, 52, 62, 28, OnChi, this);
        peng_btn_ = MakeButton(center, "碰", 76, 52, 62, 28, OnPeng, this);
        hu_btn_ = MakeButton(center, "胡", 144, 52, 62, 28, OnHu, this);
        pass_btn_ = MakeButton(center, "过", 212, 52, 62, 28, OnPass, this);
        lv_obj_add_flag(chi_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(peng_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hu_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pass_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    struct ZoneUi {
        lv_obj_t* panel = nullptr;
        lv_obj_t* name = nullptr;
        lv_obj_t* hand_row = nullptr;
        lv_obj_t* discard_row = nullptr;
        lv_obj_t* meld = nullptr;
        lv_obj_t* discard_cells[10] = {};
        lv_obj_t* discard_labels[10] = {};
        bool vertical = false;
        bool own = false;
        int seat = 0;
    };

    void CreateZone(int index, int x, int y, int w, int h, bool vertical, bool own) {
        ZoneUi& zone = zones_[index];
        zone.vertical = vertical;
        zone.own = own;
        zone.panel = MakePanel(game_screen_, x, y, w, h);
        zone.name = MakeLabel(zone.panel, "", &font_puhui_16_4, COLOR_INK, 8, 1, w - 16);

        zone.hand_row = lv_obj_create(zone.panel);
        lv_obj_set_pos(zone.hand_row, 8, 18);
        lv_obj_set_size(zone.hand_row, w - 16, vertical ? 136 : (own ? 42 : 34));
        lv_obj_set_style_bg_opa(zone.hand_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(zone.hand_row, 0, 0);
        lv_obj_set_style_pad_all(zone.hand_row, 0, 0);
        lv_obj_set_style_pad_column(zone.hand_row, 2, 0);
        lv_obj_set_style_pad_row(zone.hand_row, 2, 0);
        lv_obj_set_flex_flow(zone.hand_row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(zone.hand_row, LV_OBJ_FLAG_SCROLLABLE);

        zone.discard_row = lv_obj_create(zone.panel);
        lv_obj_set_pos(zone.discard_row, 8, vertical ? 158 : (own ? 62 : 54));
        lv_obj_set_size(zone.discard_row, w - 16, vertical ? 80 : 22);
        lv_obj_set_style_bg_opa(zone.discard_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(zone.discard_row, 0, 0);
        lv_obj_set_style_pad_all(zone.discard_row, 0, 0);
        lv_obj_set_style_pad_column(zone.discard_row, 2, 0);
        lv_obj_set_style_pad_row(zone.discard_row, 2, 0);
        lv_obj_set_flex_flow(zone.discard_row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(zone.discard_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < 10; ++i) {
            lv_obj_t* cell = lv_obj_create(zone.discard_row);
            lv_obj_set_size(cell, vertical ? 34 : 34, vertical ? 18 : 20);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xEFE8DA), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0xD8CDB9), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_radius(cell, 3, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            zone.discard_cells[i] = cell;
            zone.discard_labels[i] = MakeLabel(cell, "", &font_puhui_14_1, COLOR_INK, 0, 1);
            lv_obj_set_size(zone.discard_labels[i], vertical ? 34 : 34, 18);
            lv_obj_set_style_text_align(zone.discard_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        }

        zone.meld = MakeLabel(zone.panel, "", &font_puhui_14_1, COLOR_MUTED, 8,
                              vertical ? 242 : (own ? 86 : 78), w - 16);
    }

    void CreateChatReturnButton() {
        return_btn_ = MakeButton(chat_screen_, "牌局", 700, 36, 84, 36,
                                 OnShowGame, this);
    }

    void UpdateSetupLocked() {
        if (setup_hint_ == nullptr) return;
        for (int i = 0; i < 2; ++i) {
            lv_obj_set_style_bg_color(type_buttons_[i],
                                      lv_color_hex(i == static_cast<int>(selected_type_)
                                                       ? COLOR_GOLD : COLOR_TABLE), 0);
        }
        for (int i = 0; i < 4; ++i) {
            lv_obj_set_style_bg_color(seat_buttons_[i],
                                      lv_color_hex(i == selected_seat_ ? COLOR_GOLD
                                                                       : COLOR_TABLE), 0);
        }
        char hint[160];
        snprintf(hint, sizeof(hint), "已选择：%s · %s家。\n%s",
                 LocalGame::TypeName(selected_type_), LocalGame::SeatName(selected_seat_),
                 selected_type_ == GameType::kMahjong
                     ? "下一步会提示通过摄像头读入手牌。"
                     : "扑克会直接生成手牌并开始。");
        lv_label_set_text(setup_hint_, hint);
    }

    void ShowGame() {
        DisplayLockGuard lock(display_);
        LoadScreen(game_screen_);
        UpdateGameLocked();
    }

    void StartSelected(bool demo, bool skip_ready) {
        if (selected_type_ == GameType::kMahjong && !skip_ready) {
            DisplayLockGuard lock(display_);
            LoadScreen(ready_screen_);
            return;
        }
        LocalGame::Instance().Start(selected_type_, selected_seat_, demo);
        ShowGame();
    }

    void UpdateZone(ZoneUi& zone, const GameView& view, int rel) {
        int seat = (view.my_seat + rel) % 4;
        const PlayerView& player = view.players[seat];
        zone.seat = seat;
        bool hidden = !view.demo && seat != view.my_seat;

        char name[96];
        snprintf(name, sizeof(name), "%s%s  %d张%s", LocalGame::SeatName(seat),
                 seat == view.current_player ? "·当前" : "",
                 player.hand_count,
                 player.meld_count > 0 ? (hidden ? " · 副露明牌" : " · 副露") : "");
        lv_label_set_text(zone.name, name);

        bool current = seat == view.current_player && !view.finished;
        lv_obj_set_style_border_color(zone.panel,
                                      lv_color_hex(current ? COLOR_GOLD : 0xD8CDB9), 0);
        lv_obj_set_style_border_width(zone.panel, current ? 2 : 1, 0);

        lv_obj_clean(zone.hand_row);
        if (hidden) {
            lv_obj_t* back = MakeLabel(zone.hand_row, "暗牌", &font_puhui_16_4,
                                       COLOR_MUTED, 0, 0);
            lv_obj_set_style_text_align(back, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(back, zone.vertical ? 108 : 120);
        } else {
            for (const auto& tile_name : player.hand) {
                int w = zone.own ? 32 : (zone.vertical ? 26 : 30);
                int h = zone.own ? 38 : (zone.vertical ? 26 : 30);
                lv_obj_t* tile = MakeMiniTile(zone.hand_row, tile_name, w, h,
                                              zone.own && view.can_discard);
                if (zone.own && view.can_discard) {
                    lv_obj_add_event_cb(tile, OnTileClicked, LV_EVENT_CLICKED, this);
                }
            }
        }

        std::string meld_text;
        for (const auto& meld : player.melds) {
            if (!meld_text.empty()) {
                meld_text += " ";
            }
            meld_text += meld;
        }
        lv_label_set_text(zone.meld, meld_text.empty() ? " " : meld_text.c_str());

        const auto& discards = player.discards;
        int start = std::max(0, static_cast<int>(discards.size()) - 10);
        for (int j = 0; j < 10; ++j) {
            int index = start + j;
            lv_obj_t* cell = zone.discard_cells[j];
            lv_obj_t* label = zone.discard_labels[j];
            if (index < static_cast<int>(discards.size())) {
                lv_label_set_text(label, discards[index].c_str());
                bool latest = view.last_discard_player == seat &&
                              index == static_cast<int>(discards.size()) - 1 &&
                              !view.last_claimed;
                lv_obj_set_style_bg_color(cell,
                                          lv_color_hex(latest ? 0xF6E7B8 : 0xFFFFFF), 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xC8B89B), 0);
            } else {
                lv_label_set_text(label, "");
                lv_obj_set_style_bg_color(cell, lv_color_hex(0xEFE8DA), 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xE0D6C5), 0);
            }
        }
    }

    void UpdateGameLocked() {
        GameView view = LocalGame::Instance().View();
        if (!view.active) {
            lv_label_set_text(game_title_, "离线牌局");
            lv_label_set_text(game_status_, "未开始");
            lv_label_set_text(turn_label_, "当前：— · 牌墙 —");
            lv_label_set_text(hint_label_, "");
            lv_label_set_text(last_label_, "最近出牌：—");
            return;
        }

        char title[80];
        snprintf(title, sizeof(title), "离线%s · %s家", LocalGame::TypeName(view.type),
                 LocalGame::SeatName(view.my_seat));
        lv_label_set_text(game_title_, title);

        char status[120];
        if (view.finished) {
            snprintf(status, sizeof(status), "第 %d 巡 · %s", view.turn, view.result.c_str());
        } else {
            snprintf(status, sizeof(status), "第 %d 巡 · 牌墙 %d", view.turn,
                     view.wall_remaining);
        }
        lv_label_set_text(game_status_, status);

        char turn[220];
        if (view.awaiting_claim) {
            snprintf(turn, sizeof(turn), "当前：%s · 你可以对 %s 选择吃/碰/胡/过",
                     view.current_name.c_str(), view.claim_tile.c_str());
        } else if (view.can_discard) {
            snprintf(turn, sizeof(turn), "当前：%s（你）· 请点手牌出牌",
                     view.current_name.c_str());
        } else if (view.my_turn) {
            snprintf(turn, sizeof(turn), "当前：%s（你）· 等待摸牌/读牌",
                     view.current_name.c_str());
        } else {
            snprintf(turn, sizeof(turn), "当前：%s · 你：%s", view.current_name.c_str(),
                     LocalGame::SeatName(view.my_seat));
        }
        lv_label_set_text(turn_label_, turn);

        if (view.finished) {
            lv_label_set_text(hint_label_, view.result.c_str());
        } else if (view.awaiting_claim) {
            lv_label_set_text(hint_label_, "点“吃/碰/胡”，或点“过”继续");
        } else if (view.can_hu) {
            lv_label_set_text(hint_label_, "已经成胡，点“胡”结算本局");
        } else if (view.can_discard) {
            lv_label_set_text(hint_label_, "点自己的手牌出牌；吃碰后手牌数会同步更新");
        } else {
            lv_label_set_text(hint_label_, "演示模式四家明牌；真实模式只显示对手牌数");
        }

        if (view.last_discard.empty()) {
            lv_label_set_text(last_label_, "最近出牌：—");
        } else {
            char last[120];
            snprintf(last, sizeof(last), "最近出牌：%s %s%s",
                     LocalGame::SeatName(view.last_discard_player),
                     view.last_discard.c_str(), view.last_claimed ? "（已吃碰）" : "");
            lv_label_set_text(last_label_, last);
        }

        for (int rel = 0; rel < 4; ++rel) {
            UpdateZone(zones_[rel], view, rel);
        }

        if (view.awaiting_claim) {
            lv_obj_clear_flag(chi_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(peng_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hu_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(pass_btn_, LV_OBJ_FLAG_HIDDEN);
            if (!view.can_chi) {
                lv_obj_add_flag(chi_btn_, LV_OBJ_FLAG_HIDDEN);
            }
            if (!view.can_peng) {
                lv_obj_add_flag(peng_btn_, LV_OBJ_FLAG_HIDDEN);
            }
            if (!view.claim_can_hu) {
                lv_obj_add_flag(hu_btn_, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (view.can_hu) {
            lv_obj_add_flag(chi_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(peng_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pass_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hu_btn_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(chi_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(peng_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hu_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pass_btn_, LV_OBJ_FLAG_HIDDEN);
        }

        if (view.awaiting_claim) {
            char claim[160];
            snprintf(claim, sizeof(claim), "可对 %s：%s%s%s", view.claim_tile.c_str(),
                     view.can_chi ? "吃 " : "", view.can_peng ? "碰 " : "",
                     view.claim_can_hu ? "胡" : "");
            lv_label_set_text(advice_action_, claim);
            lv_obj_add_flag(advice_reason_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(advice_meta_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(advice_reason_, "");
            lv_label_set_text(advice_meta_, "");
        } else {
            lv_obj_clear_flag(advice_reason_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(advice_meta_, LV_OBJ_FLAG_HIDDEN);
            UpdateAdviceLocked();
        }
    }

    void UpdateAdviceLocked() {
        AdviceView advice = LocalGame::Instance().Advice();
        lv_label_set_text(advice_action_, advice.action.c_str());
        lv_label_set_text(advice_reason_, advice.reason.c_str());
        lv_label_set_text(advice_meta_, advice.meta.c_str());
    }

    static void OnTileClicked(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        // 事件从牌面文字冒泡到牌块，current_target 才是注册回调的牌块；
        // 用 target 会取到文字子对象，index 恒为 0。
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int index = lv_obj_get_index(target);
        std::string error;
        bool ok = LocalGame::Instance().DiscardIndex(
            LocalGame::Instance().my_seat(), index, &error);
        DisplayLockGuard lock(self->display_);
        if (!ok) {
            lv_label_set_text(self->hint_label_, error.c_str());
            return;
        }
        self->UpdateGameLocked();
    }

    static void OnChi(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        std::string error;
        bool ok = LocalGame::Instance().Claim(LocalGame::Instance().my_seat(), "chi", &error);
        DisplayLockGuard lock(self->display_);
        if (!ok) {
            lv_label_set_text(self->hint_label_, error.c_str());
            return;
        }
        self->UpdateGameLocked();
    }

    static void OnPeng(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        std::string error;
        bool ok = LocalGame::Instance().Claim(LocalGame::Instance().my_seat(), "peng", &error);
        DisplayLockGuard lock(self->display_);
        if (!ok) {
            lv_label_set_text(self->hint_label_, error.c_str());
            return;
        }
        self->UpdateGameLocked();
    }

    static void OnHu(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        std::string error;
        GameView view = LocalGame::Instance().View();
        bool ok = view.awaiting_claim
                      ? LocalGame::Instance().Claim(LocalGame::Instance().my_seat(), "hu", &error)
                      : LocalGame::Instance().Hu(&error);
        DisplayLockGuard lock(self->display_);
        if (!ok) {
            lv_label_set_text(self->hint_label_, error.c_str());
            return;
        }
        self->UpdateGameLocked();
    }

    static void OnPass(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        std::string error;
        bool ok = LocalGame::Instance().PassClaim(&error);
        DisplayLockGuard lock(self->display_);
        if (!ok) {
            lv_label_set_text(self->hint_label_, error.c_str());
            return;
        }
        self->UpdateGameLocked();
    }

    static void OnSort(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        LocalGame::Instance().SortMyHand();
        DisplayLockGuard lock(self->display_);
        self->UpdateGameLocked();
    }

    void TaskLoop() {
        while (true) {
            bool active = LocalGame::Instance().active();
            bool demo = LocalGame::Instance().demo();
            bool changed = false;
            if (active) {
                // Tick 推进 AI 三家；真实模式轮到玩家时只停住，不替玩家摸打。
                changed = LocalGame::Instance().Tick();
                if (!demo) {
                    GameView view = LocalGame::Instance().View();
                    if (view.my_turn && !view.awaiting_discard &&
                        !view.awaiting_claim && !view.finished) {
                        changed = LocalGame::Instance().DrawForMe() || changed;
                    }
                }
            }
            // 演示模式自动摸打；正式模式等待摄像头/语音通过 MCP 更新，
            // 这里定时刷新，保证语音录入的牌能马上显示出来。
            if (active && (!demo || changed)) {
                DisplayLockGuard lock(display_);
                if (lv_screen_active() == ready_screen_) {
                    // 语音/摄像头已经通过 MCP 录入 13 张手牌，自动进牌局。
                    LoadScreen(game_screen_);
                    UpdateGameLocked();
                } else if (lv_screen_active() == game_screen_) {
                    UpdateGameLocked();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    }

    static void OnOpenSetup(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        DisplayLockGuard lock(self->display_);
        self->LoadScreen(self->setup_screen_);
    }
    static void OnDemo(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        LocalGame::Instance().Start(self->selected_type_, self->selected_seat_, true);
        self->ShowGame();
    }
    static void OnTypeMahjong(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_type_ = GameType::kMahjong;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnTypePoker(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_type_ = GameType::kPoker;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnSeatEast(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_seat_ = 0;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnSeatSouth(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_seat_ = 1;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnSeatWest(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_seat_ = 2;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnSeatNorth(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->selected_seat_ = 3;
        DisplayLockGuard lock(self->display_);
        self->UpdateSetupLocked();
    }
    static void OnSetupNext(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        if (self->selected_type_ == GameType::kPoker) {
            self->StartSelected(false, true);
        } else {
            DisplayLockGuard lock(self->display_);
            self->LoadScreen(self->ready_screen_);
        }
    }
    static void OnSdRead(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        std::vector<std::string> tiles;
        std::string error;
        if (!CameraCards::Instance().LoadMahjongHand(&tiles, &error)) {
            DisplayLockGuard lock(self->display_);
            if (self->ready_hint_ != nullptr) lv_label_set_text(self->ready_hint_, error.c_str());
            return;
        }
        std::string raw;
        for (size_t i = 0; i < tiles.size(); ++i) {
            if (i != 0) raw += ",";
            raw += tiles[i];
        }
        if (!LocalGame::Instance().SetHand(LocalGame::Instance().my_seat(), raw, &error)) {
            DisplayLockGuard lock(self->display_);
            if (self->ready_hint_ != nullptr) {
                lv_label_set_text(self->ready_hint_, ("读牌失败：" + error).c_str());
            }
            return;
        }
        self->ShowGame();
    }

    static void OnHandReady(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        if (LocalGame::Instance().active() &&
            LocalGame::Instance().type() == GameType::kMahjong) {
            self->ShowGame();
            return;
        }
        DisplayLockGuard lock(self->display_);
        if (self->ready_hint_ != nullptr) {
            lv_label_set_text(self->ready_hint_,
                              "还没有读到你的手牌。\n\n"
                              "请通过摄像头读入，或者点右上角“语音”\n"
                              "对小智报牌，例如：\n"
                              "“我手里有一万、二万、三条……”\n"
                              "读完后小智会直接进入牌局。");
        }
    }
    static void OnBackSetup(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        DisplayLockGuard lock(self->display_);
        self->LoadScreen(self->setup_screen_);
    }
    static void OnMenu(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        LocalGame::Instance().Stop();
        DisplayLockGuard lock(self->display_);
        self->LoadScreen(self->menu_screen_);
    }
    static void OnAdvice(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        DisplayLockGuard lock(self->display_);
        self->UpdateAdviceLocked();
    }
    static void OnChat(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        DisplayLockGuard lock(self->display_);
        self->LoadScreen(self->chat_screen_);
    }
    static void OnShowGame(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        DisplayLockGuard lock(self->display_);
        if (LocalGame::Instance().active()) {
            self->LoadScreen(self->game_screen_);
            self->UpdateGameLocked();
        } else {
            self->LoadScreen(self->menu_screen_);
        }
    }

    Display* display_;
    lv_obj_t* chat_screen_ = nullptr;
    lv_obj_t* menu_screen_ = nullptr;
    lv_obj_t* setup_screen_ = nullptr;
    lv_obj_t* ready_screen_ = nullptr;
    lv_obj_t* game_screen_ = nullptr;
    lv_obj_t* return_btn_ = nullptr;
    lv_obj_t* menu_status_ = nullptr;
    lv_obj_t* setup_hint_ = nullptr;
    lv_obj_t* ready_hint_ = nullptr;
    lv_obj_t* game_title_ = nullptr;
    lv_obj_t* game_status_ = nullptr;
    lv_obj_t* turn_label_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    lv_obj_t* last_label_ = nullptr;
    lv_obj_t* advice_action_ = nullptr;
    lv_obj_t* advice_reason_ = nullptr;
    lv_obj_t* advice_meta_ = nullptr;
    lv_obj_t* chi_btn_ = nullptr;
    lv_obj_t* peng_btn_ = nullptr;
    lv_obj_t* hu_btn_ = nullptr;
    lv_obj_t* pass_btn_ = nullptr;
    ZoneUi zones_[4] = {};
    lv_obj_t* type_buttons_[2] = {};
    lv_obj_t* seat_buttons_[4] = {};
    GameType selected_type_ = GameType::kMahjong;
    int selected_seat_ = 0;
    TaskHandle_t task_ = nullptr;
};

static MahjongUiImpl* g_mahjong_ui = nullptr;

void MahjongUi::Init(Display* display) {
    if (g_mahjong_ui != nullptr) {
        return;
    }
    g_mahjong_ui = new MahjongUiImpl(display);
    g_mahjong_ui->Init();
    g_mahjong_ui->StartTask();
    ESP_LOGI(TAG, "offline Mahjong UI started");
}

void MahjongUi::Toggle() {
    if (g_mahjong_ui != nullptr) {
        g_mahjong_ui->Toggle();
    }
}
