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
    std::vector<std::string> melds;
    std::vector<std::string> hand;  // 演示模式显示四家；正式模式只显示自己
    int hand_count = 0;
    int meld_count = 0;
};

struct GameView {
    bool active = false;
    bool finished = false;
    bool demo = false;
    GameType type = GameType::kMahjong;
    int my_seat = 0;
    int current_player = 0;
    int wall_remaining = 0;
    int turn = 0;
    int winner = -1;
    bool my_turn = false;
    bool awaiting_discard = false;
    bool can_discard = false;
    bool can_hu = false;
    bool awaiting_claim = false;
    bool can_chi = false;
    bool can_peng = false;
    bool claim_can_hu = false;
    bool last_claimed = false;
    std::string current_name;
    std::string last_discard;
    std::string claim_tile;
    std::string result;
    int last_discard_player = -1;
    int claim_from = -1;
    std::vector<std::string> my_hand;
    std::array<PlayerView, 4> players;
};

struct AdviceView {
    std::string action;
    std::string reason;
    std::string meta;
    int discard_index = -1;
};

// 完全运行在 4.3C 上的离线牌局。没有 HTTP、没有后端、没有 WiFi 依赖。
class LocalGame {
public:
    static LocalGame& Instance();

    // 开始一局。demo=true 时全部座位自动摸打，但轮到自己摸牌后会停住，
    // 等待用户点手牌出牌；false 时手牌由摄像头/语音读入后进入。
    void Start(GameType type, int my_seat, bool demo);

    // 推进一局。demo 模式下会自动摸打；轮到自己时只摸牌并停住等待出牌。
    // 返回 true 表示状态发生变化。
    bool Tick();
    bool DrawForMe();

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

    // 语音/摄像头录入：支持“一万、二万、三条”或 1W/1B/1P/EA 等代码。
    bool SetHand(int player, const std::string& raw, std::string* error = nullptr);

    // 语音/摄像头补记一张弃牌；不会重复摸牌。
    bool RecordDiscard(int player, const std::string& raw, std::string* error = nullptr);

    // 出牌：index 是当前排序后手牌的下标；success 后会自动轮转并检测吃碰。
    bool DiscardIndex(int player, int index, std::string* error = nullptr);

    // 出牌：按牌名指定，自动找第一张匹配的牌。
    bool DiscardTile(int player, const std::string& raw, std::string* error = nullptr);

    // 吃/碰/胡最近一张弃牌。kind: "chi"/"peng"/"hu"/"吃"/"碰"/"胡"。
    bool Claim(int player, const std::string& kind, std::string* error = nullptr);
    bool PassClaim(std::string* error = nullptr);
    bool Hu(std::string* error = nullptr);  // 自己摸到胡牌

    bool SortHand(int player);
    bool SortMyHand();

    std::string StateText() const;
    std::string StateTextFor(int seat) const;
    std::string AdviceText() const;
    std::string AdviceTextFor(int seat) const;

    static const char* TypeName(GameType type);
    static const char* SeatName(int seat);

private:
    struct Meld {
        int kind = 1;  // 0=吃, 1=碰
        std::array<int, 3> tiles{};
    };

    LocalGame() = default;
    ~LocalGame() = default;
    LocalGame(const LocalGame&) = delete;
    LocalGame& operator=(const LocalGame&) = delete;

    void StartLocked(GameType type, int my_seat, bool demo);
    bool TickLocked();
    bool SetHandLocked(int player, const std::string& raw, std::string* error);
    bool RecordDiscardLocked(int player, const std::string& raw, std::string* error);
    bool DiscardIndexLocked(int player, int index, std::string* error);
    bool ClaimLocked(int player, const std::string& kind, std::string* error);
    bool ApplyClaimLocked(int player, int kind, int discarder, int tile);
    bool TryResolveClaimsLocked(int discarder, int tile);
    bool ShouldClaimLocked(int player, int kind, int tile) const;
    bool CanWinLocked(int player, int extra_tile = -1) const;
    bool CanHuOnDiscardLocked(int player, int tile) const;
    void FinishWinLocked(int player, int tile, bool self_draw);
    void HandleDiscardLocked(int player, int tile);
    void PassClaimLocked();
    bool HuLocked(int player, int tile, bool self_draw, std::string* error);
    int CountTileLocked(int tile) const;
    int RemainingVisibleLocked(int tile) const;
    bool ValidateLocked(std::string* error = nullptr) const;
    void AdvanceAfterDiscardLocked(int player);
    bool DrawForPlayerLocked(int player);
    int ChooseDiscardLocked(int player) const;
    int TargetHandSizeLocked(int player) const;
    void SortHandLocked(int player);
    bool CanClaimLocked(int player, int kind, int tile) const;
    void BuildMeldsLocked(int player, PlayerView* out) const;
    AdviceView AdviceLocked(int seat) const;
    GameView ViewLocked(int seat) const;
    std::string StateTextLocked(int seat) const;

    mutable std::mutex mutex_;
    bool active_ = false;
    bool finished_ = false;
    bool demo_ = false;
    bool waiting_for_discard_ = false;
    bool last_claimed_ = false;
    bool awaiting_claim_ = false;
    bool claim_can_chi_ = false;
    bool claim_can_peng_ = false;
    bool claim_can_hu_ = false;
    int claim_from_ = -1;
    int claim_tile_ = -1;
    int winner_ = -1;
    std::string result_;
    GameType type_ = GameType::kMahjong;
    int my_seat_ = 0;
    int current_player_ = 0;
    int turn_ = 1;
    int last_player_ = -1;
    int last_tile_ = -1;
    std::vector<int> wall_;
    std::array<std::vector<int>, 4> hands_;
    std::array<std::vector<int>, 4> discards_;
    std::array<std::vector<Meld>, 4> melds_;
};
