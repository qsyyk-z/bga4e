#include "mahjong_mcp.h"

#include <cJSON.h>
#include <esp_log.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "http.h"
#include "mcp_server.h"
#include "sdkconfig.h"

#define TAG "MahjongMcp"

#ifndef CONFIG_BGA4E_BACKEND_URL
#define CONFIG_BGA4E_BACKEND_URL "http://192.168.1.100:8000"
#endif

#ifndef CONFIG_BGA4E_PLAYER
#define CONFIG_BGA4E_PLAYER 0
#endif

namespace {

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
        throw std::runtime_error("网络还没有连接");
    }
    auto http = network->CreateHttp(3);
    http->SetTimeout(10000);
    if (method == "POST") {
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body));
    }
    std::string url = BaseUrl() + path;
    if (!http->Open(method, url)) {
        throw std::runtime_error("连不上牌局服务 " + url);
    }
    int code = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    if (code != 200) {
        throw std::runtime_error("牌局服务返回 " + std::to_string(code) +
                                 "：" + response.substr(0, 160));
    }
    return response;
}

std::unique_ptr<cJSON, decltype(&cJSON_Delete)> ParseJson(const std::string& text) {
    cJSON* root = cJSON_Parse(text.c_str());
    if (root == nullptr) {
        throw std::runtime_error("牌局服务返回了非法 JSON");
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
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

bool JsonBool(const cJSON* root, const char* key, bool fallback = false) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

std::string JoinArray(const cJSON* array, const std::string& sep = "、", int limit = 32) {
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

std::string ComposeAdvice(const std::string& response) {
    auto root = ParseJson(response);
    if (JsonBool(root.get(), "needs_hand")) {
        return "还没有录入手牌。请先告诉我你的 13 张或 14 张手牌，例如：一万、二万、三万……";
    }
    std::string action = JsonString(root.get(), "action", "none");
    std::string tile = JsonString(root.get(), "tile_name");
    std::string reason = JsonString(root.get(), "reason");
    int shanten = JsonInt(root.get(), "shanten", 99);
    int ukeire = JsonInt(root.get(), "ukeire", 0);

    std::string text;
    if (action == "discard") {
        text = "建议打" + (tile.empty() ? "这一张" : tile) + "。";
    } else if (action == "draw") {
        text = "建议先摸牌。";
    } else if (action == "peng") {
        text = "可以碰" + tile + "。";
    } else if (action == "chi") {
        text = "可以吃" + tile + "。";
    } else if (action == "gang") {
        text = "可以杠" + tile + "。";
    } else if (action == "win") {
        text = "已经可以胡牌了。";
    } else if (action == "pass") {
        text = "建议先过。";
    } else {
        text = "现在还没有明确的出牌建议。";
    }
    if (!reason.empty()) {
        text += reason;
        bool ends_with_cn_period = reason.size() >= 3 &&
                                   reason.compare(reason.size() - 3, 3, "。") == 0;
        if (!ends_with_cn_period && reason.back() != '!' && reason.back() != '?') {
            text += "。";
        }
    }
    if (shanten >= 0 && shanten <= 8) {
        text += "当前 " + std::to_string(shanten) + " 向听";
        if (ukeire > 0) {
            text += "，进张约 " + std::to_string(ukeire) + " 张";
        }
        text += "。";
    }
    return text;
}

std::string ComposeState(const std::string& response) {
    auto root = ParseJson(response);
    std::string current = JsonString(root.get(), "current_name", "当前玩家");
    int wall = JsonInt(root.get(), "wall_remaining", 0);
    std::string text = "当前是" + current + "行动，牌墙还有 " + std::to_string(wall) + " 张。";

    const cJSON* hand_names = cJSON_GetObjectItemCaseSensitive(root.get(), "my_hand_names");
    std::string hand = JoinArray(hand_names);
    if (hand.empty()) {
        text += "还没有录入手牌。";
    } else {
        text += "我的手牌是：" + hand + "。";
    }

    const cJSON* last = cJSON_GetObjectItemCaseSensitive(root.get(), "last_discard");
    if (cJSON_IsObject(last)) {
        std::string last_name = JsonString(last, "name", "一张牌");
        int last_player = JsonInt(last, "player", -1);
        std::string player_name;
        const cJSON* players = cJSON_GetObjectItemCaseSensitive(root.get(), "players");
        const cJSON* p = nullptr;
        if (cJSON_IsArray(players)) {
            cJSON_ArrayForEach(p, players) {
                if (JsonInt(p, "seat", -1) == last_player) {
                    player_name = JsonString(p, "name");
                    break;
                }
            }
        }
        text += "最近出牌：" + (player_name.empty() ? "有玩家" : player_name) +
                "打了" + last_name + "。";
    }

    const cJSON* players = cJSON_GetObjectItemCaseSensitive(root.get(), "players");
    const cJSON* p = nullptr;
    std::string discards;
    if (cJSON_IsArray(players)) {
        cJSON_ArrayForEach(p, players) {
            const cJSON* names = cJSON_GetObjectItemCaseSensitive(p, "discard_names");
            std::string line = JoinArray(names, "、", 8);
            if (!line.empty()) {
                discards += JsonString(p, "name") + "：" + line + "；";
            }
        }
    }
    if (!discards.empty()) {
        text += "各家弃牌：" + discards;
    }
    return text;
}

std::vector<std::string> SplitTiles(const std::string& raw) {
    // 先把中文分隔符替换成 ASCII 逗号，避免逐字节比较 UTF-8 字符
    std::string normalized = raw;
    for (const auto* sep : {"，", "、", "；"}) {
        size_t pos = 0;
        while ((pos = normalized.find(sep, pos)) != std::string::npos) {
            normalized.replace(pos, std::string(sep).size(), ",");
            pos += 1;
        }
    }

    std::vector<std::string> tiles;
    std::string current;
    for (char ch : normalized) {
        if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!current.empty()) {
                tiles.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tiles.push_back(current);
    }
    return tiles;
}

std::string BuildHandJson(const std::vector<std::string>& tiles) {
    cJSON* root = cJSON_CreateObject();
    cJSON* array = cJSON_AddArrayToObject(root, "tiles");
    for (const auto& tile : tiles) {
        cJSON_AddItemToArray(array, cJSON_CreateString(tile.c_str()));
    }
    char* raw = cJSON_PrintUnformatted(root);
    std::string body = raw == nullptr ? "{}" : raw;
    cJSON_free(raw);
    cJSON_Delete(root);
    return body;
}

std::string BuildAdviceJson(int player) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "player", player);
    cJSON_AddBoolToObject(root, "use_llm", false);  // 让 xiaozhi 的 LLM 负责组织语言
    char* raw = cJSON_PrintUnformatted(root);
    std::string body = raw == nullptr ? "{}" : raw;
    cJSON_free(raw);
    cJSON_Delete(root);
    return body;
}

}  // namespace

void AddMahjongMcpTools() {
    static bool added = false;
    if (added) {
        return;
    }
    added = true;

    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.mahjong.get_state",
        "获取当前麻将牌局：我的手牌、各家弃牌、牌墙剩余、当前玩家、最近出牌。"
        "当用户问“现在牌局怎么样”“我有什么牌”“谁出过什么”“牌墙还剩多少”时调用。",
        PropertyList({
            Property("player", kPropertyTypeInteger, CONFIG_BGA4E_PLAYER, 0, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            try {
                std::string response = HttpRequest(
                    "GET", "/api/current/summary?player=" + std::to_string(player));
                return ComposeState(response);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "get_state failed: %s", e.what());
                return std::string("暂时连不上牌局服务，请检查后端地址和 WiFi。");
            }
        });

    mcp_server.AddTool(
        "self.mahjong.get_advice",
        "根据当前牌局给出打牌建议：该打哪张、理由、向听和进张。"
        "当用户问“我该打什么”“给我点建议”“怎么打”时，必须优先调用此工具，"
        "不要自己凭空猜牌。",
        PropertyList({
            Property("player", kPropertyTypeInteger, CONFIG_BGA4E_PLAYER, 0, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            try {
                std::string response = HttpRequest(
                    "POST", "/api/current/advice", BuildAdviceJson(player));
                return ComposeAdvice(response);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "get_advice failed: %s", e.what());
                return std::string("暂时连不上牌局服务，请检查后端地址和 WiFi。");
            }
        });

    mcp_server.AddTool(
        "self.mahjong.set_hand",
        "录入用户自己的手牌。当用户报出手牌（例如“我手里有一万、二万、三万……”）时调用。"
        "参数 tiles 是逗号或空格分隔的牌代码：万 1W-9W，条 1B-9B，筒 1P-9P，"
        "东 EA、南 SO、西 WE、北 NO、中 CE、发 GR、白 WH。",
        PropertyList({
            Property("tiles", kPropertyTypeString),
            Property("player", kPropertyTypeInteger, CONFIG_BGA4E_PLAYER, 0, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            std::string raw = properties["tiles"].value<std::string>();
            auto tiles = SplitTiles(raw);
            if (tiles.empty()) {
                return std::string("没有听清手牌，请再说一遍，例如：一万、二万、三万。");
            }
            try {
                std::string body = BuildHandJson(tiles);
                HttpRequest("POST", "/api/current/hands/" + std::to_string(player), body);
                return "手牌已录入，共 " + std::to_string(tiles.size()) + " 张。";
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "set_hand failed: %s", e.what());
                return std::string("手牌录入失败，请检查牌局服务。");
            }
        });

    mcp_server.AddTool(
        "self.mahjong.record_discard",
        "记录一张打出的牌。当用户明确说“我打三万/记一下这张牌”，"
        "而摄像头没有自动记录时调用。牌代码规则同 set_hand。",
        PropertyList({
            Property("tile", kPropertyTypeString),
            Property("player", kPropertyTypeInteger, CONFIG_BGA4E_PLAYER, 0, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string tile = properties["tile"].value<std::string>();
            int player = properties["player"].value<int>();
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "discard");
            cJSON_AddNumberToObject(root, "player", player);
            cJSON_AddStringToObject(root, "tile", tile.c_str());
            cJSON_AddStringToObject(root, "source", "xiaozhi");
            char* raw = cJSON_PrintUnformatted(root);
            std::string body = raw == nullptr ? "{}" : raw;
            cJSON_free(raw);
            cJSON_Delete(root);
            try {
                HttpRequest("POST", "/api/current/events", body);
                return "已记录打出 " + tile + "。";
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "record_discard failed: %s", e.what());
                return std::string("记录失败：可能是还没轮到该玩家，或牌局服务不可用。");
            }
        });

    ESP_LOGI(TAG, "BGA4E mahjong MCP tools registered");
}
