#include "local_game.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>
#include <esp_log.h>
#include <esp_random.h>
#include <random>
#include <sstream>

#define TAG "LocalGame"

namespace {

constexpr int kMahjongKinds = 34;
constexpr int kPokerCards = 52;

const char* kMahjongNames[kMahjongKinds] = {
    "一万", "二万", "三万", "四万", "五万", "六万", "七万", "八万", "九万",
    "一条", "二条", "三条", "四条", "五条", "六条", "七条", "八条", "九条",
    "一筒", "二筒", "三筒", "四筒", "五筒", "六筒", "七筒", "八筒", "九筒",
    "东", "南", "西", "北", "中", "发", "白",
};

const char* kSeatNames[4] = {"东", "南", "西", "北"};

std::string Upper(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string Trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::string Join(const std::vector<int>& tiles, int limit, bool poker) {
    std::string out;
    int count = 0;
    for (int tile : tiles) {
        if (count >= limit) {
            out += "…";
            break;
        }
        if (!out.empty()) {
            out += "、";
        }
        if (poker) {
            static const char* suits[4] = {"黑", "红", "梅", "方"};
            static const char* ranks[13] = {"2", "3", "4", "5", "6", "7", "8", "9",
                                            "10", "J", "Q", "K", "A"};
            if (tile >= 0 && tile < kPokerCards) {
                out += ranks[tile % 13];
                out += suits[tile / 13];
            }
        } else if (tile >= 0 && tile < kMahjongKinds) {
            out += kMahjongNames[tile];
        }
        count++;
    }
    return out;
}

std::string TileName(int tile, bool poker) {
    return Join({tile}, 1, poker);
}

std::string NormalizeSeparators(std::string raw) {
    for (const char* sep : {"，", "、", "；"}) {
        size_t pos = 0;
        while ((pos = raw.find(sep, pos)) != std::string::npos) {
            raw.replace(pos, std::string(sep).size(), ",");
            pos += 1;
        }
    }
    return raw;
}

std::vector<std::string> SplitTiles(const std::string& raw) {
    std::string normalized = NormalizeSeparators(raw);
    std::vector<std::string> out;
    std::string current;
    for (char ch : normalized) {
        if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!current.empty()) {
                out.push_back(Trim(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        out.push_back(Trim(current));
    }
    return out;
}

int ParseMahjongTile(const std::string& raw) {
    std::string value = Trim(raw);
    if (value.empty()) {
        return -1;
    }
    if (value == "红中") {
        return 31;
    }
    if (value == "幺鸡") {
        return 9;
    }
    if (value == "一饼") {
        return 18;
    }
    for (int i = 0; i < kMahjongKinds; ++i) {
        if (value == kMahjongNames[i]) {
            return i;
        }
    }
    std::string upper = Upper(value);
    if (upper == "EA") return 27;
    if (upper == "SO") return 28;
    if (upper == "WE") return 29;
    if (upper == "NO") return 30;
    if (upper == "CE") return 31;
    if (upper == "GR") return 32;
    if (upper == "WH") return 33;
    if (upper.size() >= 2 && upper[0] >= '1' && upper[0] <= '9') {
        int number = upper[0] - '1';
        switch (upper[1]) {
            case 'W': return number;
            case 'B': return 9 + number;
            case 'P': return 18 + number;
            default: break;
        }
    }
    return -1;
}

}  // namespace

LocalGame& LocalGame::Instance() {
    static LocalGame game;
    return game;
}

const char* LocalGame::TypeName(GameType type) {
    return type == GameType::kPoker ? "扑克" : "麻将";
}

const char* LocalGame::SeatName(int seat) {
    if (seat < 0 || seat > 3) {
        return "未知";
    }
    return kSeatNames[seat];
}

void LocalGame::Start(GameType type, int my_seat, bool demo) {
    std::lock_guard<std::mutex> lock(mutex_);
    StartLocked(type, my_seat, demo);
}

void LocalGame::StartLocked(GameType type, int my_seat, bool demo) {
    type_ = type;
    my_seat_ = std::max(0, std::min(3, my_seat));
    demo_ = demo;
    active_ = true;
    finished_ = false;
    current_player_ = my_seat_;
    turn_ = 1;
    last_player_ = -1;
    last_tile_ = -1;
    wall_.clear();
    for (auto& hand : hands_) hand.clear();
    for (auto& discard : discards_) discard.clear();

    std::vector<int> deck;
    if (type_ == GameType::kMahjong) {
        for (int kind = 0; kind < kMahjongKinds; ++kind) {
            for (int copy = 0; copy < 4; ++copy) {
                deck.push_back(kind);
            }
        }
    } else {
        for (int card = 0; card < kPokerCards; ++card) {
            deck.push_back(card);
        }
    }

    std::mt19937 rng(esp_random());
    std::shuffle(deck.begin(), deck.end(), rng);

    int hand_size = type_ == GameType::kMahjong ? 13 : 5;
    for (int player = 0; player < 4; ++player) {
        for (int i = 0; i < hand_size; ++i) {
            hands_[player].push_back(deck.back());
            deck.pop_back();
        }
    }
    wall_ = std::move(deck);
    ESP_LOGI(TAG, "started %s game, seat %s, wall %d", TypeName(type_),
             SeatName(my_seat_), static_cast<int>(wall_.size()));
}

void LocalGame::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    finished_ = false;
    wall_.clear();
    for (auto& hand : hands_) hand.clear();
    for (auto& discard : discards_) discard.clear();
}

bool LocalGame::Tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    return TickLocked();
}

bool LocalGame::TickLocked() {
    if (!active_ || finished_) {
        return false;
    }
    if (wall_.empty()) {
        finished_ = true;
        ESP_LOGI(TAG, "game finished, wall empty");
        return true;
    }

    int player = current_player_;
    int tile = wall_.back();
    wall_.pop_back();
    hands_[player].push_back(tile);

    int discard_index = ChooseDiscardLocked(player, static_cast<int>(hands_[player].size()));
    if (discard_index < 0 || discard_index >= static_cast<int>(hands_[player].size())) {
        discard_index = static_cast<int>(hands_[player].size()) - 1;
    }
    int discarded = hands_[player][discard_index];
    hands_[player].erase(hands_[player].begin() + discard_index);
    discards_[player].push_back(discarded);
    last_tile_ = discarded;
    last_player_ = player;

    current_player_ = (current_player_ + 1) % 4;
    if (current_player_ == my_seat_) {
        turn_++;
    }
    return true;
}

bool LocalGame::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

GameType LocalGame::type() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return type_;
}

int LocalGame::my_seat() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return my_seat_;
}

bool LocalGame::demo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return demo_;
}

int LocalGame::ChooseDiscardLocked(int player, int /*hand_size*/) const {
    const auto& hand = hands_[player];
    if (hand.empty()) {
        return -1;
    }
    if (type_ == GameType::kPoker) {
        int best = 0;
        for (int i = 1; i < static_cast<int>(hand.size()); ++i) {
            int rank_i = hand[i] % 13;
            int rank_best = hand[best] % 13;
            if (rank_i < rank_best) {
                best = i;
            }
        }
        return best;
    }

    int best = 0;
    int best_score = 1000;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        int tile = hand[i];
        int same = 0;
        int neighbor = 0;
        int gap = 0;
        for (int j = 0; j < static_cast<int>(hand.size()); ++j) {
            if (i == j) continue;
            int other = hand[j];
            if (other == tile) {
                same++;
            } else if (tile < 27 && other < 27 && tile / 9 == other / 9) {
                int distance = std::abs((tile % 9) - (other % 9));
                if (distance == 1) neighbor++;
                if (distance == 2) gap++;
            }
        }
        int score = same * 6 + neighbor * 3 + gap;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

bool LocalGame::SetHand(int player, const std::string& raw, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return SetHandLocked(player, raw, error);
}

bool LocalGame::SetHandLocked(int player, const std::string& raw, std::string* error) {
    if (player < 0 || player > 3) {
        if (error) *error = "座位号必须是 0-3。";
        return false;
    }
    if (type_ == GameType::kPoker && active_) {
        if (error) *error = "扑克牌局不需要读入手牌。";
        return false;
    }
    auto tokens = SplitTiles(raw);
    if (tokens.empty()) {
        if (error) *error = "没有听清手牌，请再说一遍。";
        return false;
    }

    std::vector<int> parsed;
    for (const auto& token : tokens) {
        int tile = ParseMahjongTile(token);
        if (tile < 0) {
            if (error) *error = "不认识的牌：" + token;
            return false;
        }
        parsed.push_back(tile);
    }
    if (parsed.size() < 13 || parsed.size() > 14) {
        if (error) *error = "麻将手牌应为 13 或 14 张，现在收到 " +
                            std::to_string(parsed.size()) + " 张。";
        return false;
    }

    if (!active_) {
        StartLocked(GameType::kMahjong, player, false);
    }
    type_ = GameType::kMahjong;
    hands_[player] = std::move(parsed);
    my_seat_ = player;
    current_player_ = player;
    ESP_LOGI(TAG, "hand set for seat %d, %d tiles", player,
             static_cast<int>(hands_[player].size()));
    return true;
}

bool LocalGame::RecordDiscard(int player, const std::string& raw, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return RecordDiscardLocked(player, raw, error);
}

bool LocalGame::RecordDiscardLocked(int player, const std::string& raw, std::string* error) {
    if (!active_ || finished_) {
        if (error) *error = "当前没有进行中的牌局。";
        return false;
    }
    if (player < 0 || player > 3) {
        if (error) *error = "座位号必须是 0-3。";
        return false;
    }
    if (type_ == GameType::kPoker) {
        if (error) *error = "扑克牌请直接在界面上自动记录。";
        return false;
    }
    int tile = ParseMahjongTile(raw);
    if (tile < 0) {
        if (error) *error = "不认识的牌：" + raw;
        return false;
    }
    auto& hand = hands_[player];
    auto it = std::find(hand.begin(), hand.end(), tile);
    if (it != hand.end()) {
        hand.erase(it);
    }
    discards_[player].push_back(tile);
    last_tile_ = tile;
    last_player_ = player;
    current_player_ = (player + 1) % 4;
    if (current_player_ == my_seat_) {
        turn_++;
    }
    return true;
}

GameView LocalGame::View() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ViewLocked(my_seat_);
}

GameView LocalGame::ViewFor(int seat) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ViewLocked(seat);
}

GameView LocalGame::ViewLocked(int seat) const {
    GameView view;
    view.active = active_;
    view.finished = finished_;
    view.type = type_;
    view.my_seat = seat;
    view.current_player = current_player_;
    view.current_name = SeatName(current_player_);
    view.wall_remaining = static_cast<int>(wall_.size());
    view.turn = turn_;
    view.my_turn = active_ && current_player_ == seat;
    view.last_discard_player = last_player_;
    if (last_tile_ >= 0) {
        view.last_discard = TileName(last_tile_, type_ == GameType::kPoker);
    }
    for (int i = 0; i < 4; ++i) {
        view.players[i].name = SeatName(i);
        view.players[i].hand_count = static_cast<int>(hands_[i].size());
        view.players[i].discards = {};
        for (int tile : discards_[i]) {
            view.players[i].discards.push_back(TileName(tile, type_ == GameType::kPoker));
        }
    }
    if (seat >= 0 && seat <= 3) {
        for (int tile : hands_[seat]) {
            view.my_hand.push_back(TileName(tile, type_ == GameType::kPoker));
        }
    }
    return view;
}

AdviceView LocalGame::Advice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return AdviceLocked(my_seat_);
}

AdviceView LocalGame::AdviceFor(int seat) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return AdviceLocked(seat);
}

AdviceView LocalGame::AdviceLocked(int seat) const {
    AdviceView advice;
    if (!active_) {
        advice.action = "尚未开始牌局";
        advice.reason = "点击“开始牌局”或“生成演示牌局”。";
        return advice;
    }
    if (finished_) {
        advice.action = "本局已结束";
        advice.reason = "牌墙已经摸完，点击“菜单”重新开始。";
        return advice;
    }
    if (seat < 0 || seat > 3 || hands_[seat].empty()) {
        advice.action = "等待手牌";
        advice.reason = "还没有读到你的手牌。";
        return advice;
    }

    int index = ChooseDiscardLocked(seat, static_cast<int>(hands_[seat].size()));
    if (index < 0 || index >= static_cast<int>(hands_[seat].size())) {
        advice.action = "暂无建议";
        return advice;
    }
    int tile = hands_[seat][index];
    std::string name = TileName(tile, type_ == GameType::kPoker);

    if (type_ == GameType::kPoker) {
        int pairs = 0;
        for (int i = 0; i < static_cast<int>(hands_[seat].size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(hands_[seat].size()); ++j) {
                if (hands_[seat][i] % 13 == hands_[seat][j] % 13) {
                    pairs++;
                }
            }
        }
        advice.action = "建议打 " + name;
        advice.reason = "这张牌点数最小，先换掉它，保留大牌和对子。";
        advice.meta = "5 张牌 · 对子 " + std::to_string(pairs);
        return advice;
    }

    int pairs = 0;
    int triplets = 0;
    int kinds[kMahjongKinds] = {0};
    for (int value : hands_[seat]) {
        if (value >= 0 && value < kMahjongKinds) {
            kinds[value]++;
        }
    }
    for (int count : kinds) {
        if (count >= 2) pairs++;
        if (count >= 3) triplets++;
    }
    int shanten = 8 - pairs * 2 - triplets;
    if (shanten < 0) shanten = 0;
    if (shanten > 8) shanten = 8;

    int ukeire = 0;
    for (int value : hands_[seat]) {
        if (value >= 27) {
            if (kinds[value] < 4) ukeire += 4 - kinds[value];
            continue;
        }
        int suit = value / 9;
        int rank = value % 9;
        for (int delta = -2; delta <= 2; ++delta) {
            int other_rank = rank + delta;
            if (delta == 0 || other_rank < 0 || other_rank > 8) continue;
            int other = suit * 9 + other_rank;
            if (kinds[other] < 4) {
                ukeire += 4 - kinds[other];
            }
        }
    }
    if (ukeire > 99) ukeire = 99;

    advice.action = "建议打 " + name;
    advice.reason = "这张牌周围的搭子最少，先打孤张，保留两面搭子。";
    advice.meta = "向听≈" + std::to_string(shanten) + " · 进张≈" +
                  std::to_string(ukeire) + " 张";
    return advice;
}

std::string LocalGame::StateText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return StateTextLocked(my_seat_);
}

std::string LocalGame::StateTextFor(int seat) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return StateTextLocked(seat);
}

std::string LocalGame::StateTextLocked(int seat) const {
    if (!active_) {
        return "当前没有进行中的牌局。点击“开始牌局”或“生成演示牌局”即可开局。";
    }
    std::string text = "这是离线" + std::string(TypeName(type_)) + "牌局，你坐在" +
                       SeatName(seat) + "。当前轮到" + SeatName(current_player_) +
                       "，牌墙还有 " + std::to_string(wall_.size()) + " 张。";
    if (finished_) {
        text += "本局已经结束。";
    }
    if (seat >= 0 && seat <= 3) {
        text += "我的手牌是：" + Join(hands_[seat], 14, type_ == GameType::kPoker) + "。";
    }
    if (last_tile_ >= 0) {
        text += "最近出牌：" + std::string(SeatName(last_player_)) + "打了" +
                TileName(last_tile_, type_ == GameType::kPoker) + "。";
    }
    std::string discard_text;
    for (int i = 0; i < 4; ++i) {
        if (discards_[i].empty()) continue;
        discard_text += std::string(SeatName(i)) + "：" +
                        Join(discards_[i], 8, type_ == GameType::kPoker) + "；";
    }
    if (!discard_text.empty()) {
        text += "各家弃牌：" + discard_text;
    }
    return text;
}

std::string LocalGame::AdviceText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AdviceView advice = AdviceLocked(my_seat_);
    std::string text = advice.action;
    if (!advice.reason.empty()) {
        text += " " + advice.reason;
    }
    if (!advice.meta.empty()) {
        text += " " + advice.meta;
    }
    return text;
}

std::string LocalGame::AdviceTextFor(int seat) const {
    std::lock_guard<std::mutex> lock(mutex_);
    AdviceView advice = AdviceLocked(seat);
    std::string text = advice.action;
    if (!advice.reason.empty()) {
        text += " " + advice.reason;
    }
    if (!advice.meta.empty()) {
        text += " " + advice.meta;
    }
    return text;
}
