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

        lv_obj_t* info = MakePanel(game_screen_, 12, 70, 500, 58);
        turn_label_ = MakeLabel(info, "当前：— · 牌墙 —", &font_puhui_20_4,
                                COLOR_INK, 14, 8, 470);
        hint_label_ = MakeLabel(info, "", &font_puhui_20_4, COLOR_MUTED, 14, 32, 470);

        lv_obj_t* players = MakePanel(game_screen_, 12, 138, 500, 190);
        MakeLabel(players, "弃牌区（最近 10 张）", &font_puhui_20_4, COLOR_MUTED, 14, 4);
        for (int i = 0; i < 4; ++i) {
            discard_names_[i] = MakeLabel(players, "", &font_puhui_20_4, COLOR_INK,
                                          4, 32 + i * 37, 62);
            for (int j = 0; j < 10; ++j) {
                lv_obj_t* cell = lv_obj_create(players);
                lv_obj_set_pos(cell, 68 + j * 42, 30 + i * 37);
                lv_obj_set_size(cell, 40, 30);
                lv_obj_set_style_bg_color(cell, lv_color_hex(0xEFE8DA), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xD8CDB9), 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_radius(cell, 4, 0);
                lv_obj_set_style_pad_all(cell, 0, 0);
                lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
                discard_cells_[i][j] = cell;
                discard_cell_labels_[i][j] = MakeLabel(cell, "", &font_puhui_20_4,
                                                       COLOR_INK, 0, 4);
                lv_obj_set_width(discard_cell_labels_[i][j], 40);
                lv_obj_set_style_text_align(discard_cell_labels_[i][j],
                                            LV_TEXT_ALIGN_CENTER, 0);
            }
        }

        lv_obj_t* last = MakePanel(game_screen_, 12, 334, 500, 64);
        MakeLabel(last, "最近出牌", &font_puhui_20_4, COLOR_MUTED, 14, 6);
        last_label_ = MakeLabel(last, "—", &font_puhui_30_4, COLOR_RED, 150, 12, 330);

        lv_obj_t* advice = MakePanel(game_screen_, 524, 70, 264, 328);
        MakeLabel(advice, "策略建议", &font_puhui_20_4, COLOR_MUTED, 14, 10);
        advice_action_ = MakeLabel(advice, "等待牌局…", &font_puhui_30_4, COLOR_TABLE,
                                   14, 46, 236);
        advice_reason_ = MakeLabel(advice, "点击“建议”获取本地建议。",
                                   &font_puhui_20_4, COLOR_INK, 14, 108, 236);
        advice_meta_ = MakeLabel(advice, "", &font_puhui_20_4, COLOR_MUTED, 14, 278, 236);

        lv_obj_t* hand_panel = MakePanel(game_screen_, 12, 406, 776, 66);
        MakeLabel(hand_panel, "手牌", &font_puhui_20_4, COLOR_MUTED, 12, 20);
        hand_row_ = lv_obj_create(hand_panel);
        lv_obj_set_pos(hand_row_, 72, 5);
        lv_obj_set_size(hand_row_, 692, 56);
        lv_obj_set_style_bg_opa(hand_row_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hand_row_, 0, 0);
        lv_obj_set_style_pad_all(hand_row_, 0, 0);
        lv_obj_set_style_pad_column(hand_row_, 3, 0);
        lv_obj_set_flex_flow(hand_row_, LV_FLEX_FLOW_ROW);
        lv_obj_remove_flag(hand_row_, LV_OBJ_FLAG_SCROLLABLE);
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

    void UpdateGameLocked() {
        GameView view = LocalGame::Instance().View();
        if (!view.active) {
            lv_label_set_text(game_title_, "离线牌局");
            lv_label_set_text(game_status_, "未开始");
            lv_label_set_text(turn_label_, "当前：— · 牌墙 —");
            lv_label_set_text(hint_label_, "");
            return;
        }

        char title[80];
        snprintf(title, sizeof(title), "离线%s · %s家", LocalGame::TypeName(view.type),
                 LocalGame::SeatName(view.my_seat));
        lv_label_set_text(game_title_, title);

        char status[96];
        if (view.finished) {
            snprintf(status, sizeof(status), "第 %d 巡 · 已结束", view.turn);
        } else {
            snprintf(status, sizeof(status), "第 %d 巡 · 牌墙 %d", view.turn,
                     view.wall_remaining);
        }
        lv_label_set_text(game_status_, status);

        char turn[180];
        if (view.can_discard) {
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
        lv_label_set_text(hint_label_, view.finished ? "本局结束，点“菜单”重新开始" : "");

        if (view.last_discard.empty()) {
            lv_label_set_text(last_label_, "—");
        } else {
            char last[120];
            snprintf(last, sizeof(last), "%s：%s%s",
                     LocalGame::SeatName(view.last_discard_player),
                     view.last_discard.c_str(), view.last_claimed ? "（已吃碰）" : "");
            lv_label_set_text(last_label_, last);
        }

        lv_obj_clean(hand_row_);
        for (const auto& name : view.my_hand) {
            lv_obj_t* tile = MakeHandTile(hand_row_, name, view.type == GameType::kPoker);
            lv_obj_add_event_cb(tile, OnTileClicked, LV_EVENT_CLICKED, this);
        }

        for (int i = 0; i < 4; ++i) {
            char name[24];
            snprintf(name, sizeof(name), "%s%d", view.players[i].name.c_str(),
                     view.players[i].hand_count);
            lv_label_set_text(discard_names_[i], name);
            const auto& discards = view.players[i].discards;
            int start = std::max(0, static_cast<int>(discards.size()) - 10);
            for (int j = 0; j < 10; ++j) {
                int index = start + j;
                lv_obj_t* cell = discard_cells_[i][j];
                lv_obj_t* label = discard_cell_labels_[i][j];
                if (index < static_cast<int>(discards.size())) {
                    lv_label_set_text(label, discards[index].c_str());
                    lv_obj_set_style_bg_color(cell, lv_color_white(), 0);
                    lv_obj_set_style_border_color(cell, lv_color_hex(0xC8B89B), 0);
                } else {
                    lv_label_set_text(label, "");
                    lv_obj_set_style_bg_color(cell, lv_color_hex(0xEFE8DA), 0);
                    lv_obj_set_style_border_color(cell, lv_color_hex(0xE0D6C5), 0);
                }
            }
        }
        UpdateAdviceLocked();
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
            if (active && demo) {
                changed = LocalGame::Instance().Tick();
            } else if (active) {
                // 正式模式只帮玩家摸牌，不替他出牌；出牌必须由触控或语音确认。
                GameView view = LocalGame::Instance().View();
                if (view.my_turn && !view.awaiting_discard && !view.finished) {
                    changed = LocalGame::Instance().DrawForMe();
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
    lv_obj_t* hand_row_ = nullptr;
    lv_obj_t* discard_names_[4] = {};
    lv_obj_t* discard_cells_[4][10] = {};
    lv_obj_t* discard_cell_labels_[4][10] = {};
    lv_obj_t* advice_action_ = nullptr;
    lv_obj_t* advice_reason_ = nullptr;
    lv_obj_t* advice_meta_ = nullptr;
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
