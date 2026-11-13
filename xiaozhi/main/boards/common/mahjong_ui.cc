#include "mahjong_ui.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "display.h"
#include "http.h"
#include "sdkconfig.h"

#define TAG "MahjongUi"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_puhui_30_4);

#ifndef CONFIG_BGA4E_BACKEND_URL
#define CONFIG_BGA4E_BACKEND_URL "http://192.168.1.100:8000"
#endif
#ifndef CONFIG_BGA4E_PLAYER
#define CONFIG_BGA4E_PLAYER 0
#endif

namespace {

constexpr uint32_t COLOR_BG = 0xF4ECDD;
constexpr uint32_t COLOR_PANEL = 0xFFFAF0;
constexpr uint32_t COLOR_INK = 0x25221D;
constexpr uint32_t COLOR_MUTED = 0x6F675B;
constexpr uint32_t COLOR_TABLE = 0x1D5A47;
constexpr uint32_t COLOR_GOLD = 0xC49A2C;
constexpr uint32_t COLOR_RED = 0xB23A2B;

std::string BaseUrl() {
    std::string url = CONFIG_BGA4E_BACKEND_URL;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string HttpRequest(const std::string& method, const std::string& path,
                        const std::string& body = "") {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        throw std::runtime_error("网络未连接");
    }
    auto http = network->CreateHttp(4);
    http->SetTimeout(8000);
    if (method == "POST") {
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body));
    }
    std::string url = BaseUrl() + path;
    if (!http->Open(method, url)) {
        throw std::runtime_error("连不上后端");
    }
    int code = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    if (code != 200) {
        throw std::runtime_error("HTTP " + std::to_string(code));
    }
    return response;
}

std::unique_ptr<cJSON, decltype(&cJSON_Delete)> ParseJson(const std::string& text) {
    cJSON* root = cJSON_Parse(text.c_str());
    if (root == nullptr) {
        throw std::runtime_error("JSON 解析失败");
    }
    return std::unique_ptr<cJSON, decltype(&cJSON_Delete)>(root, cJSON_Delete);
}

std::string JsonString(const cJSON* root, const char* key, const std::string& fallback = "") {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
    }
    return fallback;
}

int JsonInt(const cJSON* root, const char* key, int fallback = 0) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

std::string JoinArray(const cJSON* array, const std::string& sep = "、", int limit = 16) {
    if (!cJSON_IsArray(array)) {
        return "";
    }
    std::string out;
    int count = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        if (count >= limit) {
            out += sep + "…";
            break;
        }
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            continue;
        }
        if (!out.empty()) {
            out += sep;
        }
        out += item->valuestring;
        count++;
    }
    return out;
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
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

lv_obj_t* MakeTile(lv_obj_t* parent, const std::string& name) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 44, 56);
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

}  // namespace

class MahjongUiImpl {
public:
    explicit MahjongUiImpl(Display* display) : display_(display) {}

    void Init() {
        DisplayLockGuard lock(display_);
        chat_screen_ = lv_screen_active();

        game_screen_ = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(game_screen_, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_bg_opa(game_screen_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(game_screen_, LV_OBJ_FLAG_SCROLLABLE);

        // 顶栏
        lv_obj_t* top = lv_obj_create(game_screen_);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, 800, 58);
        lv_obj_set_style_bg_color(top, lv_color_hex(COLOR_TABLE), 0);
        lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(top, 0, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

        MakeLabel(top, "BGA4E 麻将助手", &font_puhui_30_4, COLOR_GOLD, 18, 10);
        status_label_ = MakeLabel(top, "连接后端中…", &font_puhui_20_4, 0xD7E6DC, 300, 18, 230);

        MakeButton(top, "刷新", 545, 11, 72, 36, OnRefresh, this);
        MakeButton(top, "建议", 625, 11, 72, 36, OnAdvice, this);
        MakeButton(top, "语音", 705, 11, 76, 36, OnChat, this);

        // 当前牌局信息
        lv_obj_t* info = MakePanel(12, 70, 500, 58);
        turn_label_ = MakeLabel(info, "当前：— · 第 0 巡 · 牌墙 —", &font_puhui_20_4,
                                COLOR_INK, 14, 14, 470);

        // 四家弃牌
        lv_obj_t* players = MakePanel(12, 138, 500, 182);
        MakeLabel(players, "各家弃牌", &font_puhui_20_4, COLOR_MUTED, 14, 8);
        for (int i = 0; i < 4; ++i) {
            player_labels_[i] = MakeLabel(players, "", &font_puhui_20_4, COLOR_INK,
                                          14, 38 + i * 34, 470);
        }

        // 最近出牌
        lv_obj_t* last = MakePanel(12, 330, 500, 68);
        MakeLabel(last, "最近出牌", &font_puhui_20_4, COLOR_MUTED, 14, 8);
        last_label_ = MakeLabel(last, "—", &font_puhui_30_4, COLOR_RED, 150, 16, 330);

        // 策略建议
        lv_obj_t* advice = MakePanel(524, 70, 264, 328);
        MakeLabel(advice, "策略建议", &font_puhui_20_4, COLOR_MUTED, 14, 10);
        advice_action_ = MakeLabel(advice, "等待牌局…", &font_puhui_30_4, COLOR_TABLE,
                                   14, 46, 236);
        advice_reason_ = MakeLabel(advice, "点击“刷新”获取当前牌局，\n点击“建议”获取出牌建议。",
                                   &font_puhui_20_4, COLOR_INK, 14, 100, 236);
        advice_meta_ = MakeLabel(advice, "", &font_puhui_20_4, COLOR_MUTED, 14, 270, 236);

        // 我的手牌
        lv_obj_t* hand_panel = MakePanel(12, 406, 776, 66);
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

        // 回到对话页的“牌局”按钮
        return_btn_ = MakeButton(chat_screen_, "牌局", 700, 36, 84, 36, OnShowGame, this);

        lv_screen_load(game_screen_);
    }

    void StartTask() {
        xTaskCreate([](void* arg) {
            static_cast<MahjongUiImpl*>(arg)->TaskLoop();
        }, "mahjong_ui", 10240, this, 3, &task_);
    }

    void Toggle() {
        DisplayLockGuard lock(display_);
        if (lv_screen_active() == game_screen_) {
            lv_screen_load(chat_screen_);
        } else {
            lv_screen_load(game_screen_);
        }
    }

private:
    lv_obj_t* MakePanel(int x, int y, int w, int h) {
        lv_obj_t* panel = lv_obj_create(game_screen_);
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

    static void OnRefresh(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->refresh_requested_.store(true);
    }
    static void OnAdvice(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        self->advice_requested_.store(true);
    }
    static void OnChat(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        lv_screen_load(self->chat_screen_);
    }
    static void OnShowGame(lv_event_t* e) {
        auto* self = static_cast<MahjongUiImpl*>(lv_event_get_user_data(e));
        lv_screen_load(self->game_screen_);
    }

    void TaskLoop() {
        while (true) {
            if (refresh_requested_.exchange(false)) {
                FetchSummary();
            }
            if (advice_requested_.exchange(false)) {
                FetchAdvice();
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }

    void FetchSummary() {
        try {
            std::string body = HttpRequest(
                "GET", "/api/current/summary?player=" + std::to_string(CONFIG_BGA4E_PLAYER));
            auto root = ParseJson(body);
            UpdateSummary(root.get());
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "summary failed: %s", e.what());
            UpdateOffline();
        }
    }

    void FetchAdvice() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "player", CONFIG_BGA4E_PLAYER);
        cJSON_AddBoolToObject(root, "use_llm", false);
        char* raw = cJSON_PrintUnformatted(root);
        std::string body = raw == nullptr ? "{}" : raw;
        cJSON_free(raw);
        cJSON_Delete(root);
        try {
            std::string response = HttpRequest("POST", "/api/current/advice", body);
            auto advice = ParseJson(response);
            UpdateAdvice(advice.get());
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "advice failed: %s", e.what());
            DisplayLockGuard lock(display_);
            lv_label_set_text(advice_reason_, "暂时连不上牌局服务，请检查后端地址和 WiFi。");
        }
    }

    void UpdateSummary(const cJSON* root) {
        DisplayLockGuard lock(display_);
        lv_label_set_text(status_label_, "已连接");

        std::string current = JsonString(root, "current_name", "—");
        int wall = JsonInt(root, "wall_remaining", 0);
        int turn = JsonInt(root, "turn", 0);
        char line[128];
        snprintf(line, sizeof(line), "当前：%s · 第 %d 巡 · 牌墙 %d",
                 current.c_str(), turn, wall);
        lv_label_set_text(turn_label_, line);

        const cJSON* last = cJSON_GetObjectItemCaseSensitive(root, "last_discard");
        if (cJSON_IsObject(last)) {
            std::string name = JsonString(last, "name", "—");
            lv_label_set_text(last_label_, name.c_str());
        } else {
            lv_label_set_text(last_label_, "—");
        }

        const cJSON* hand = cJSON_GetObjectItemCaseSensitive(root, "my_hand_names");
        lv_obj_clean(hand_row_);
        if (cJSON_IsArray(hand)) {
            const cJSON* item = nullptr;
            int count = 0;
            cJSON_ArrayForEach(item, hand) {
                if (count++ >= 14) {
                    break;
                }
                if (cJSON_IsString(item) && item->valuestring != nullptr) {
                    MakeTile(hand_row_, item->valuestring);
                }
            }
        }

        const cJSON* players = cJSON_GetObjectItemCaseSensitive(root, "players");
        if (cJSON_IsArray(players)) {
            const cJSON* p = nullptr;
            int idx = 0;
            cJSON_ArrayForEach(p, players) {
                if (idx >= 4) {
                    break;
                }
                std::string name = JsonString(p, "name", "玩家");
                std::string discards = JoinArray(
                    cJSON_GetObjectItemCaseSensitive(p, "discard_names"), "、", 8);
                std::string text = name + "：" + (discards.empty() ? "—" : discards);
                lv_label_set_text(player_labels_[idx], text.c_str());
                idx++;
            }
        }
    }

    void UpdateOffline() {
        DisplayLockGuard lock(display_);
        lv_label_set_text(status_label_, "后端未连接");
        lv_label_set_text(advice_reason_, "连不上后端。请确认：\n1. PC 后端已启动\n2. 同一 WiFi\n3. menuconfig 中的 IP");
    }

    void UpdateAdvice(const cJSON* root) {
        DisplayLockGuard lock(display_);
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "needs_hand"))) {
            lv_label_set_text(advice_action_, "请先录入手牌");
            lv_label_set_text(advice_reason_, "对小智说：\n“我的手牌是……”");
            lv_label_set_text(advice_meta_, "");
            return;
        }
        std::string action = JsonString(root, "action", "none");
        std::string tile = JsonString(root, "tile_name");
        std::string reason = JsonString(root, "reason");
        int shanten = JsonInt(root, "shanten", 99);
        int ukeire = JsonInt(root, "ukeire", 0);

        std::string action_text;
        if (action == "discard") {
            action_text = "建议打 " + tile;
        } else if (action == "draw") {
            action_text = "建议摸牌";
        } else if (action == "peng") {
            action_text = "可以碰 " + tile;
        } else if (action == "chi") {
            action_text = "可以吃 " + tile;
        } else if (action == "gang") {
            action_text = "可以杠 " + tile;
        } else if (action == "win") {
            action_text = "已经可以胡牌";
        } else {
            action_text = "暂无建议";
        }
        lv_label_set_text(advice_action_, action_text.c_str());
        lv_label_set_text(advice_reason_, reason.c_str());

        char meta[96] = "";
        if (shanten >= 0 && shanten <= 8) {
            snprintf(meta, sizeof(meta), "向听 %d · 进张 %d 张", shanten, ukeire);
        }
        lv_label_set_text(advice_meta_, meta);
    }

    Display* display_;
    lv_obj_t* game_screen_ = nullptr;
    lv_obj_t* chat_screen_ = nullptr;
    lv_obj_t* return_btn_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* turn_label_ = nullptr;
    lv_obj_t* last_label_ = nullptr;
    lv_obj_t* hand_row_ = nullptr;
    lv_obj_t* player_labels_[4] = {};
    lv_obj_t* advice_action_ = nullptr;
    lv_obj_t* advice_reason_ = nullptr;
    lv_obj_t* advice_meta_ = nullptr;
    std::atomic<bool> refresh_requested_{true};
    std::atomic<bool> advice_requested_{false};
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
    ESP_LOGI(TAG, "Mahjong UI started");
}

void MahjongUi::Toggle() {
    if (g_mahjong_ui != nullptr) {
        g_mahjong_ui->Toggle();
    }
}
