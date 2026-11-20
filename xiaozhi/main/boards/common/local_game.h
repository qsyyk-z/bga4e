#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

enum class GameType {
    kMahjong = 0,
    kPoker = 1,
};

struct PlayerView {
    std::string name;
    std::vector<std::string> discards;
    int hand_count = 0;
};

struct GameView {
    bool active = false;
    bool finished = false;
    GameType type = GameType::kMahjong;
    int my_seat = 0;
    int current_player = 0;
    int wall_remaining = 0;
    int turn = 0;
    bool my_turn = false;
    std::string current_name;
    std::string last_discard;
    int last_discard_player = -1;
    std::vector<std::string> my_hand;
    std::array<PlayerView, 4> players;
};

struct AdviceView {
    std::string action;
    std::string reason;
    std::string meta;
};

// 完全运行在 4.3C 上的离线牌局。没有 HTTP、没有后端、没有 WiFi 依赖。
class LocalGame {
public:
    static LocalGame& Instance();

    // 开始一局。demo=true 时全部座位自动摸打；false 时同样自动推进，
    // 但手牌被视为已经由摄像头/用户读入。
    void Start(GameType type, int my_seat, bool demo);

    // 自动推进一轮：当前玩家摸一张、打一张，然后轮到下家。
    // 返回 true 表示状态发生变化。
    bool Tick();

    // 停止当前牌局，回到未开始状态。
    void Stop();

    bool active() const;
    GameType type() const;
    int my_seat() const;
    bool demo() const;

    GameView View() const;
    GameView ViewFor(int seat) const;
    AdviceView Advice() const;
    AdviceView AdviceFor(int seat) const;

    // 语音录入：支持“一万、二万、三条”或 1W/1B/1P/EA 等代码。
    bool SetHand(int player, const std::string& raw, std::string* error = nullptr);

    // 语音/摄像头补记一张弃牌；不会重复摸牌。
    bool RecordDiscard(int player, const std::string& raw, std::string* error = nullptr);

    std::string StateText() const;
    std::string StateTextFor(int seat) const;
    std::string AdviceText() const;
    std::string AdviceTextFor(int seat) const;

    static const char* TypeName(GameType type);
    static const char* SeatName(int seat);

private:
    LocalGame() = default;
    ~LocalGame() = default;
    LocalGame(const LocalGame&) = delete;
    LocalGame& operator=(const LocalGame&) = delete;

    void StartLocked(GameType type, int my_seat, bool demo);
    bool TickLocked();
    bool SetHandLocked(int player, const std::string& raw, std::string* error);
    bool RecordDiscardLocked(int player, const std::string& raw, std::string* error);
    int ChooseDiscardLocked(int player, int hand_size) const;
    AdviceView AdviceLocked(int seat) const;
    GameView ViewLocked(int seat) const;
    std::string StateTextLocked(int seat) const;

    mutable std::mutex mutex_;
    bool active_ = false;
    bool finished_ = false;
    bool demo_ = false;
    GameType type_ = GameType::kMahjong;
    int my_seat_ = 0;
    int current_player_ = 0;
    int turn_ = 1;
    int last_player_ = -1;
    int last_tile_ = -1;
    std::vector<int> wall_;
    std::array<std::vector<int>, 4> hands_;
    std::array<std::vector<int>, 4> discards_;
};
