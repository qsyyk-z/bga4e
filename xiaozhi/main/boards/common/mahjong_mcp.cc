#include "mahjong_mcp.h"

#include <esp_log.h>

#include <string>

#include "local_game.h"
#include "mcp_server.h"
#include "sdkconfig.h"

#define TAG "MahjongMcp"

#ifndef CONFIG_BGA4E_PLAYER
#define CONFIG_BGA4E_PLAYER 0
#endif

void AddMahjongMcpTools() {
    static bool added = false;
    if (added) {
        return;
    }
    added = true;

    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.mahjong.get_state",
        "获取当前离线牌局：我的手牌、各家弃牌、牌墙剩余、当前玩家、最近出牌。"
        "当用户问“现在牌局怎么样”“我有什么牌”“谁出过什么”“牌墙还剩多少”时调用。"
        "不需要网络，牌局完全在 4.3C 上运行。",
        PropertyList({
            Property("player", kPropertyTypeInteger, -1, -1, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            if (player < 0) player = LocalGame::Instance().my_seat();
            return LocalGame::Instance().StateTextFor(player);
        });

    mcp_server.AddTool(
        "self.mahjong.get_advice",
        "根据当前离线牌局给出打牌建议：该打哪张、理由、向听和进张。"
        "当用户问“我该打什么”“给我点建议”“怎么打”时，必须优先调用此工具，"
        "不要自己凭空猜牌。牌局不依赖后端。",
        PropertyList({
            Property("player", kPropertyTypeInteger, -1, -1, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            if (player < 0) player = LocalGame::Instance().my_seat();
            return LocalGame::Instance().AdviceTextFor(player);
        });

    mcp_server.AddTool(
        "self.mahjong.set_hand",
        "录入用户自己的手牌（离线牌局）。当用户报出手牌，例如“我手里有一万、二万、三条”时调用。"
        "参数 tiles 是逗号或空格分隔的牌：一万到九万、一条到九条、一筒到九筒、东、南、西、北、中、发、白，"
        "也支持 1W-9W、1B-9B、1P-9P、EA/SO/WE/NO/CE/GR/WH。",
        PropertyList({
            Property("tiles", kPropertyTypeString),
            Property("player", kPropertyTypeInteger, -1, -1, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int player = properties["player"].value<int>();
            if (player < 0) player = LocalGame::Instance().my_seat();
            std::string raw = properties["tiles"].value<std::string>();
            std::string error;
            if (!LocalGame::Instance().SetHand(player, raw, &error)) {
                return "手牌录入失败：" + error;
            }
            return std::string("手牌已录入。现在可以对小智说“给我建议”。");
        });

    mcp_server.AddTool(
        "self.mahjong.record_discard",
        "记录一张打出的牌。当用户明确说“我打三万/记一下这张牌”，"
        "而界面没有自动记录时调用。牌名规则同 set_hand。",
        PropertyList({
            Property("tile", kPropertyTypeString),
            Property("player", kPropertyTypeInteger, -1, -1, 3),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string tile = properties["tile"].value<std::string>();
            int player = properties["player"].value<int>();
            if (player < 0) player = LocalGame::Instance().my_seat();
            std::string error;
            if (!LocalGame::Instance().RecordDiscard(player, tile, &error)) {
                return "记录失败：" + error;
            }
            return "已记录打出 " + tile + "。";
        });

    mcp_server.AddTool(
        "self.mahjong.discard_tile",
        "在用户自己的回合打出一张手牌。用户说“打三万”“出这张”“打掉五条”时调用。"
        "牌名规则同 set_hand；只能打自己手里有的牌。",
        PropertyList({
            Property("tile", kPropertyTypeString),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string tile = properties["tile"].value<std::string>();
            std::string error;
            if (!LocalGame::Instance().DiscardTile(LocalGame::Instance().my_seat(), tile, &error)) {
                return "出牌失败：" + error;
            }
            return "已打出 " + tile + "。";
        });

    mcp_server.AddTool(
        "self.mahjong.claim",
        "吃或碰上一张弃牌。用户说“碰”“吃”“我碰这张”时调用。"
        "kind 支持 chi/吃 或 peng/碰；只有手牌满足条件且上一张弃牌还在时才能成功。",
        PropertyList({
            Property("kind", kPropertyTypeString),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string kind = properties["kind"].value<std::string>();
            std::string error;
            if (!LocalGame::Instance().Claim(LocalGame::Instance().my_seat(), kind, &error)) {
                return "吃碰失败：" + error;
            }
            return "已完成" + kind + "，现在请打出一张牌。";
        });

    mcp_server.AddTool(
        "self.mahjong.sort_hand",
        "整理自己的手牌。用户说“理牌”“整理一下手牌”时调用。",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            LocalGame::Instance().SortMyHand();
            return "手牌已整理。";
        });

    ESP_LOGI(TAG, "offline mahjong MCP tools registered");
}
