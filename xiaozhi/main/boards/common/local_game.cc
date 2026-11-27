#include "local_game.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <utility>
#include <esp_log.h>
#include <esp_random.h>
#include <random>

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
    // 摄像头协议可能输出阿拉伯数字 + 花色，例如 5万 / 5条 / 5筒。
    if (value.size() >= 4 && value[0] >= '1' && value[0] <= '9') {
        const std::string suffix = value.substr(1);
        int number = value[0] - '1';
        if (suffix == "万") return number;
        if (suffix == "条") return 9 + number;
        if (suffix == "筒") return 18 + number;
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

bool IsPokerRankFirst(int a, int b) {
    int rank_a = a % 13;
    int rank_b = b % 13;
    if (rank_a != rank_b) return rank_a < rank_b;
    return a / 13 < b / 13;
}

// 标准型和牌向听：4 个面子 + 1 个雀头。exposed 是已经吃/碰的面子数。
int ShantenFromCounts(int counts[kMahjongKinds], int exposed) {
    int best = 8;
    std::function<void(int, int, int)> dfs = [&](int pos, int melds, int partials) {
        if (exposed + melds > 4) {
            return;
        }
        while (pos < kMahjongKinds && counts[pos] == 0) {
            ++pos;
        }
        if (pos >= kMahjongKinds) {
            int m = std::min(4, exposed + melds);
            int t = std::min(partials, 4 - m);
            int shanten = 8 - 2 * m - t;
            if (shanten < best) {
                best = shanten;
            }
            return;
        }

        // 当作孤张跳过。
        counts[pos]--;
        dfs(pos, melds, partials);
        counts[pos]++;

        // 刻子。
        if (counts[pos] >= 3) {
            counts[pos] -= 3;
            dfs(pos, melds + 1, partials);
            counts[pos] += 3;
        }

        // 顺子。
        if (pos < 27 && pos % 9 <= 6 && counts[pos + 1] > 0 && counts[pos + 2] > 0) {
            counts[pos]--;
            counts[pos + 1]--;
            counts[pos + 2]--;
            dfs(pos, melds + 1, partials);
            counts[pos]++;
            counts[pos + 1]++;
            counts[pos + 2]++;
        }

        // 对子/搭子。
        if (counts[pos] >= 2) {
            counts[pos] -= 2;
            dfs(pos, melds, partials + 1);
            counts[pos] += 2;
        }
        if (pos < 27 && pos % 9 <= 7 && counts[pos + 1] > 0) {
            counts[pos]--;
            counts[pos + 1]--;
            dfs(pos, melds, partials + 1);
            counts[pos]++;
            counts[pos + 1]++;
        }
        if (pos < 27 && pos % 9 <= 6 && counts[pos + 2] > 0) {
            counts[pos]--;
            counts[pos + 2]--;
            dfs(pos, melds, partials + 1);
            counts[pos]++;
            counts[pos + 2]++;
        }
    };
    dfs(0, 0, 0);
    return std::max(0, std::min(8, best));
}

bool FindChiTiles(const std::vector<int>& hand, int tile, std::array<int, 3>* out) {
    if (tile < 0 || tile >= 27) return false;
    int suit = tile / 9;
    int rank = tile % 9;
    for (int start = rank - 2; start <= rank; ++start) {
        if (start < 0 || start + 2 > 8) continue;
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            int t = suit * 9 + start + k;
            if (t == tile) continue;  // 这张来自弃牌，不需要在手里
            if (std::count(hand.begin(), hand.end(), t) < 1) {
                ok = false;
                break;
            }
        }
        if (ok) {
            (*out) = {suit * 9 + start, suit * 9 + start + 1, suit * 9 + start + 2};
            return true;
        }
    }
    return false;
}

// 递归把 counts 拆成 need 个面子；用于标准型胡牌判断。
void CountHand(const std::vector<int>& hand, int counts[kMahjongKinds]);

bool TakeMelds(int counts[kMahjongKinds], int need) {
    if (need == 0) {
        for (int i = 0; i < kMahjongKinds; ++i) {
            if (counts[i] != 0) return false;
        }
        return true;
    }
    int pos = 0;
    while (pos < kMahjongKinds && counts[pos] == 0) ++pos;
    if (pos >= kMahjongKinds) return false;
    if (counts[pos] >= 3) {
        counts[pos] -= 3;
        if (TakeMelds(counts, need - 1)) {
            counts[pos] += 3;
            return true;
        }
        counts[pos] += 3;
    }
    if (pos < 27 && pos % 9 <= 6 && counts[pos + 1] > 0 && counts[pos + 2] > 0) {
        counts[pos]--;
        counts[pos + 1]--;
        counts[pos + 2]--;
        if (TakeMelds(counts, need - 1)) {
            counts[pos]++;
            counts[pos + 1]++;
            counts[pos + 2]++;
            return true;
        }
        counts[pos]++;
        counts[pos + 1]++;
        counts[pos + 2]++;
    }
    return false;
}

bool CanWinHand(const std::vector<int>& hand, int exposed) {
    if (exposed < 0 || exposed > 4) return false;
    int need = 4 - exposed;
    if (static_cast<int>(hand.size()) != need * 3 + 2) return false;
    int counts[kMahjongKinds];
    CountHand(hand, counts);
    for (int pair = 0; pair < kMahjongKinds; ++pair) {
        if (counts[pair] < 2) continue;
        counts[pair] -= 2;
        if (TakeMelds(counts, need)) {
            counts[pair] += 2;
            return true;
        }
        counts[pair] += 2;
    }
    // 七对：只在门清时判断。
    if (exposed == 0 && static_cast<int>(hand.size()) == 14) {
        int pairs = 0;
        for (int i = 0; i < kMahjongKinds; ++i) {
            if (counts[i] % 2 != 0) return false;
            if (counts[i] == 2) pairs++;
        }
        if (pairs == 7) return true;
    }
    return false;
}


void CountHand(const std::vector<int>& hand, int counts[kMahjongKinds]) {
    for (int i = 0; i < kMahjongKinds; ++i) counts[i] = 0;
    for (int tile : hand) {
        if (tile >= 0 && tile < kMahjongKinds) counts[tile]++;
    }
}

int IsolationScore(const std::vector<int>& hand, int skip, int tile) {
    int same = 0;
    int neighbor = 0;
    int gap = 0;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        if (i == skip) continue;
        int other = hand[i];
        if (other == tile) {
            same++;
        } else if (tile < 27 && other < 27 && tile / 9 == other / 9) {
            int distance = std::abs((tile % 9) - (other % 9));
            if (distance == 1) neighbor++;
            if (distance == 2) gap++;
        }
    }
    return same * 6 + neighbor * 3 + gap;
}

int BestMahjongDiscard(const std::vector<int>& hand, int exposed, int* shanten_out) {
    if (hand.empty()) return -1;
    int best = 0;
    int best_shanten = 99;
    int best_isolation = 999;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        std::vector<int> remain = hand;
        remain.erase(remain.begin() + i);
        int counts[kMahjongKinds];
        CountHand(remain, counts);
        int shanten = ShantenFromCounts(counts, exposed);
        int isolation = IsolationScore(hand, i, hand[i]);
        if (shanten < best_shanten ||
            (shanten == best_shanten && isolation < best_isolation)) {
            best = i;
            best_shanten = shanten;
            best_isolation = isolation;
        }
    }
    if (shanten_out != nullptr) *shanten_out = best_shanten;
    return best;
}

int PokerKeepScore(const std::vector<int>& cards) {
    int ranks[13] = {0};
    int suits[4] = {0};
    for (int card : cards) {
        ranks[card % 13]++;
        suits[card / 13]++;
    }
    int pairs = 0;
    int trips = 0;
    int quads = 0;
    for (int count : ranks) {
        if (count == 2) pairs++;
        if (count == 3) trips++;
        if (count >= 4) quads++;
    }
    int max_suit = *std::max_element(suits, suits + 4);
    int score = pairs * 16 + trips * 36 + quads * 70;
    if (max_suit >= 5) score += 50;
    else if (max_suit == 4) score += 14;

    int straight = 0;
    for (int start = 0; start <= 8; ++start) {
        int run = 0;
        for (int i = start; i < 13 && ranks[i] > 0; ++i) run++;
        straight = std::max(straight, run);
    }
    if (straight >= 5) score += 40;
    else if (straight == 4) score += 10;

    int high = 0;
    for (int card : cards) high += card % 13;
    return score * 2 + high;
}

int BestPokerDiscard(const std::vector<int>& hand, int* reason_out) {
    if (hand.empty()) return -1;
    int best = 0;
    int best_keep = -1;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        std::vector<int> remain = hand;
        remain.erase(remain.begin() + i);
        int keep = PokerKeepScore(remain);
        if (keep > best_keep) {
            best_keep = keep;
            best = i;
        }
    }
    if (reason_out != nullptr) *reason_out = best_keep;
    return best;
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
    waiting_for_discard_ = false;
    last_claimed_ = false;
    awaiting_claim_ = false;
    claim_can_chi_ = false;
    claim_can_peng_ = false;
    claim_can_hu_ = false;
    claim_from_ = -1;
    claim_tile_ = -1;
    winner_ = -1;
    result_.clear();
    current_player_ = my_seat_;
    turn_ = 1;
    last_player_ = -1;
    last_tile_ = -1;
    wall_.clear();
    for (auto& hand : hands_) hand.clear();
    for (auto& discard : discards_) discard.clear();
    for (auto& meld : melds_) meld.clear();

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
        SortHandLocked(player);
    }
    wall_ = std::move(deck);
    ESP_LOGI(TAG, "started %s game, seat %s, wall %d", TypeName(type_),
             SeatName(my_seat_), static_cast<int>(wall_.size()));
    ValidateLocked();
}

void LocalGame::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    finished_ = false;
    waiting_for_discard_ = false;
    awaiting_claim_ = false;
    winner_ = -1;
    result_.clear();
    wall_.clear();
    for (auto& hand : hands_) hand.clear();
    for (auto& discard : discards_) discard.clear();
    for (auto& meld : melds_) meld.clear();
}

bool LocalGame::Tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    return TickLocked();
}

bool LocalGame::DrawForMe() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || finished_ || demo_ || awaiting_claim_ || waiting_for_discard_ ||
        current_player_ != my_seat_) {
        return false;
    }
    if (!DrawForPlayerLocked(my_seat_)) {
        finished_ = true;
        result_ = "牌墙摸完，流局";
        return false;
    }
    waiting_for_discard_ = true;
    return true;
}

bool LocalGame::TickLocked() {
    if (!active_ || finished_) {
        return false;
    }
    if (awaiting_claim_) {
        return false;  // 等玩家选择吃/碰/胡/过
    }

    int player = current_player_;
    if (player == my_seat_) {
        // 正式模式由 DrawForMe() 摸牌；演示模式在这里摸牌后停住。
        if (!demo_) return false;
    }
    if (!waiting_for_discard_) {
        if (wall_.empty()) {
            finished_ = true;
            result_ = "牌墙摸完，流局";
            ESP_LOGI(TAG, "game finished, wall empty");
            return true;
        }
        if (!DrawForPlayerLocked(player)) {
            finished_ = true;
            result_ = "牌墙摸完，流局";
            return true;
        }
        waiting_for_discard_ = true;
        if (CanWinLocked(player)) {
            if (player == my_seat_) {
                return true;  // 等玩家点“胡”或继续出牌
            }
            FinishWinLocked(player, hands_[player].back(), true);
            return true;
        }
        if (player == my_seat_) {
            ESP_LOGI(TAG, "demo stopped on my turn, waiting for touch discard");
            return true;
        }
    } else if (player == my_seat_) {
        return false;  // 等用户点手牌出牌或胡
    }

    int index = ChooseDiscardLocked(player);
    if (index < 0 || index >= static_cast<int>(hands_[player].size())) {
        index = static_cast<int>(hands_[player].size()) - 1;
    }
    int discarded = hands_[player][index];
    hands_[player].erase(hands_[player].begin() + index);
    waiting_for_discard_ = false;
    HandleDiscardLocked(player, discarded);
    return true;
}

bool LocalGame::DrawForPlayerLocked(int player) {
    if (wall_.empty()) return false;
    int tile = wall_.back();
    wall_.pop_back();
    hands_[player].push_back(tile);
    SortHandLocked(player);
    return true;
}

void LocalGame::HandleDiscardLocked(int player, int tile) {
    discards_[player].push_back(tile);
    last_tile_ = tile;
    last_player_ = player;
    last_claimed_ = false;
    waiting_for_discard_ = false;

    if (type_ == GameType::kMahjong && TryResolveClaimsLocked(player, tile)) {
        return;
    }
    current_player_ = (player + 1) % 4;
    if (current_player_ == my_seat_) {
        turn_++;
    }
}

void LocalGame::AdvanceAfterDiscardLocked(int player) {
    HandleDiscardLocked(player, last_tile_);
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

int LocalGame::TargetHandSizeLocked(int player) const {
    if (type_ == GameType::kPoker) return 5;
    return 13 - 3 * static_cast<int>(melds_[player].size());
}

void LocalGame::SortHandLocked(int player) {
    auto& hand = hands_[player];
    if (type_ == GameType::kPoker) {
        std::sort(hand.begin(), hand.end(), IsPokerRankFirst);
    } else {
        std::sort(hand.begin(), hand.end());
    }
}

bool LocalGame::SortHand(int player) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (player < 0 || player > 3) return false;
    SortHandLocked(player);
    return true;
}

bool LocalGame::SortMyHand() {
    std::lock_guard<std::mutex> lock(mutex_);
    SortHandLocked(my_seat_);
    return true;
}

int LocalGame::ChooseDiscardLocked(int player) const {
    const auto& hand = hands_[player];
    if (hand.empty()) return -1;
    if (type_ == GameType::kPoker) {
        return BestPokerDiscard(hand, nullptr);
    }
    return BestMahjongDiscard(hand, static_cast<int>(melds_[player].size()), nullptr);
}

bool LocalGame::CanClaimLocked(int player, int kind, int tile) const {
    if (player < 0 || player > 3 || player == last_player_ || tile < 0) return false;
    if (type_ != GameType::kMahjong) return false;
    if (last_tile_ != tile || discards_[last_player_].empty()) return false;
    if (discards_[last_player_].back() != tile) return false;
    const auto& hand = hands_[player];
    if (kind == 1) {
        return std::count(hand.begin(), hand.end(), tile) >= 2;
    }
    std::array<int, 3> chi{};
    return FindChiTiles(hand, tile, &chi);
}

bool LocalGame::ApplyClaimLocked(int player, int kind, int discarder, int tile) {
    auto& hand = hands_[player];
    auto& source = discards_[discarder];
    if (source.empty() || source.back() != tile) return false;

    std::array<int, 3> meld_tiles{};
    if (kind == 1) {
        int removed = 0;
        for (int i = 0; i < static_cast<int>(hand.size()) && removed < 2;) {
            if (hand[i] == tile) {
                hand.erase(hand.begin() + i);
                removed++;
            } else {
                i++;
            }
        }
        if (removed != 2) return false;
        meld_tiles = {tile, tile, tile};
    } else {
        std::array<int, 3> chi{};
        if (!FindChiTiles(hand, tile, &chi)) return false;
        for (int t : chi) {
            if (t == tile) continue;
            auto it = std::find(hand.begin(), hand.end(), t);
            if (it == hand.end()) return false;
            hand.erase(it);
        }
        meld_tiles = chi;
    }

    source.pop_back();
    melds_[player].push_back(Meld{kind, meld_tiles});
    last_player_ = player;
    last_tile_ = tile;
    last_claimed_ = true;
    current_player_ = player;
    waiting_for_discard_ = true;
    awaiting_claim_ = false;
    claim_can_chi_ = claim_can_peng_ = claim_can_hu_ = false;
    SortHandLocked(player);
    return true;
}

bool LocalGame::CanWinLocked(int player, int extra_tile) const {
    if (player < 0 || player > 3 || type_ != GameType::kMahjong) return false;
    std::vector<int> hand = hands_[player];
    if (extra_tile >= 0) hand.push_back(extra_tile);
    return CanWinHand(hand, static_cast<int>(melds_[player].size()));
}

bool LocalGame::CanHuOnDiscardLocked(int player, int tile) const {
    return CanWinLocked(player, tile);
}

int LocalGame::CountTileLocked(int tile) const {
    int count = 0;
    for (const auto& hand : hands_) {
        count += static_cast<int>(std::count(hand.begin(), hand.end(), tile));
    }
    for (const auto& pile : discards_) {
        count += static_cast<int>(std::count(pile.begin(), pile.end(), tile));
    }
    for (const auto& melds : melds_) {
        for (const auto& meld : melds) {
            count += static_cast<int>(std::count(meld.tiles.begin(), meld.tiles.end(), tile));
        }
    }
    count += static_cast<int>(std::count(wall_.begin(), wall_.end(), tile));
    return count;
}

int LocalGame::RemainingVisibleLocked(int tile) const {
    int max_copies = type_ == GameType::kPoker ? 1 : 4;
    int seen = 0;
    for (const auto& hand : hands_) {
        seen += static_cast<int>(std::count(hand.begin(), hand.end(), tile));
    }
    for (const auto& pile : discards_) {
        seen += static_cast<int>(std::count(pile.begin(), pile.end(), tile));
    }
    for (const auto& melds : melds_) {
        for (const auto& meld : melds) {
            seen += static_cast<int>(std::count(meld.tiles.begin(), meld.tiles.end(), tile));
        }
    }
    return std::max(0, max_copies - seen);
}

bool LocalGame::ValidateLocked(std::string* error) const {
    int max_copies = type_ == GameType::kPoker ? 1 : 4;
    int kinds = type_ == GameType::kPoker ? kPokerCards : kMahjongKinds;
    for (int tile = 0; tile < kinds; ++tile) {
        int count = CountTileLocked(tile);
        if (count > max_copies) {
            if (error) {
                *error = "牌张数量异常：" + TileName(tile, type_ == GameType::kPoker) +
                         " 出现 " + std::to_string(count) + " 张。";
            }
            ESP_LOGE(TAG, "tile invariant broken: %s count=%d", TileName(tile, false).c_str(),
                     count);
            return false;
        }
    }
    return true;
}

bool LocalGame::ShouldClaimLocked(int player, int kind, int tile) const {
    if (!CanClaimLocked(player, kind, tile)) return false;
    int before = 8;
    BestMahjongDiscard(hands_[player], static_cast<int>(melds_[player].size()), &before);
    std::vector<int> after = hands_[player];
    if (kind == 1) {
        int removed = 0;
        for (int i = 0; i < static_cast<int>(after.size()) && removed < 2;) {
            if (after[i] == tile) {
                after.erase(after.begin() + i);
                removed++;
            } else {
                i++;
            }
        }
        if (removed != 2) return false;
    } else {
        std::array<int, 3> chi{};
        if (!FindChiTiles(after, tile, &chi)) return false;
        for (int t : chi) {
            if (t == tile) continue;
            auto it = std::find(after.begin(), after.end(), t);
            if (it == after.end()) return false;
            after.erase(it);
        }
    }
    int after_shanten = 8;
    BestMahjongDiscard(after, static_cast<int>(melds_[player].size()) + 1, &after_shanten);
    return after_shanten < before;
}

bool LocalGame::TryResolveClaimsLocked(int discarder, int tile) {
    if (type_ != GameType::kMahjong || tile < 0) return false;
    for (int offset = 1; offset < 4; ++offset) {
        int player = (discarder + offset) % 4;
        if (player == discarder) continue;
        if (player == my_seat_) {
            bool hu = CanHuOnDiscardLocked(player, tile);
            bool chi = CanClaimLocked(player, 0, tile);
            bool peng = CanClaimLocked(player, 1, tile);
            if (hu || chi || peng) {
                awaiting_claim_ = true;
                claim_from_ = discarder;
                claim_tile_ = tile;
                claim_can_chi_ = chi;
                claim_can_peng_ = peng;
                claim_can_hu_ = hu;
                current_player_ = discarder;
                ESP_LOGI(TAG, "player claim window: hu=%d chi=%d peng=%d tile=%s", hu, chi,
                         peng, TileName(tile, false).c_str());
                return true;
            }
            continue;
        }
        if (CanHuOnDiscardLocked(player, tile)) {
            FinishWinLocked(player, tile, false);
            return true;
        }
        // AI 先碰后吃；只有能改善向听才吃碰。
        if (ShouldClaimLocked(player, 1, tile) &&
            ApplyClaimLocked(player, 1, discarder, tile)) {
            ESP_LOGI(TAG, "seat %d peng %s", player, TileName(tile, false).c_str());
            return true;
        }
        if (ShouldClaimLocked(player, 0, tile) &&
            ApplyClaimLocked(player, 0, discarder, tile)) {
            ESP_LOGI(TAG, "seat %d chi %s", player, TileName(tile, false).c_str());
            return true;
        }
    }
    return false;
}

void LocalGame::FinishWinLocked(int player, int tile, bool self_draw) {
    if (player < 0 || player > 3) return;
    if (!self_draw && last_player_ >= 0 && last_player_ < 4 && !discards_[last_player_].empty() &&
        discards_[last_player_].back() == tile) {
        discards_[last_player_].pop_back();
        hands_[player].push_back(tile);
        SortHandLocked(player);
    }
    finished_ = true;
    winner_ = player;
    waiting_for_discard_ = false;
    awaiting_claim_ = false;
    claim_can_chi_ = claim_can_peng_ = claim_can_hu_ = false;
    current_player_ = player;
    last_tile_ = tile;
    last_player_ = player;
    last_claimed_ = true;
    result_ = std::string(SeatName(player)) + (self_draw ? " 自摸 " : " 胡 ") +
              TileName(tile, false);
    ESP_LOGI(TAG, "%s", result_.c_str());
}

bool LocalGame::HuLocked(int player, int tile, bool self_draw, std::string* error) {
    if (!active_ || finished_) {
        if (error) *error = "当前没有进行中的牌局。";
        return false;
    }
    if (type_ != GameType::kMahjong) {
        if (error) *error = "扑克牌局不支持胡牌。";
        return false;
    }
    if (!CanWinLocked(player, self_draw ? -1 : tile)) {
        if (error) *error = "这手牌还没有成胡。";
        return false;
    }
    FinishWinLocked(player, tile, self_draw);
    return true;
}

bool LocalGame::Hu(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!waiting_for_discard_ || current_player_ != my_seat_) {
        if (error) *error = "还没轮到你胡牌。";
        return false;
    }
    int tile = hands_[my_seat_].empty() ? -1 : hands_[my_seat_].back();
    return HuLocked(my_seat_, tile, true, error);
}

void LocalGame::PassClaimLocked() {
    if (!awaiting_claim_) return;
    awaiting_claim_ = false;
    claim_can_chi_ = claim_can_peng_ = claim_can_hu_ = false;
    int from = claim_from_;
    claim_from_ = -1;
    claim_tile_ = -1;
    current_player_ = (from + 1) % 4;
    if (current_player_ == my_seat_) turn_++;
}

bool LocalGame::PassClaim(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!awaiting_claim_) {
        if (error) *error = "当前没有可吃碰的牌。";
        return false;
    }
    PassClaimLocked();
    return true;
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

    if (!active_ || type_ != GameType::kMahjong) {
        StartLocked(GameType::kMahjong, player, false);
    }
    type_ = GameType::kMahjong;
    my_seat_ = player;
    demo_ = false;

    // 重建整副牌：先扣掉读入的 13/14 张，再给另外三家发牌。
    // 这样牌墙/手牌/弃牌/副露里同一张牌永远不会超过 4 张。
    std::vector<int> deck;
    for (int kind = 0; kind < kMahjongKinds; ++kind) {
        for (int copy = 0; copy < 4; ++copy) {
            deck.push_back(kind);
        }
    }
    for (int tile : parsed) {
        auto it = std::find(deck.begin(), deck.end(), tile);
        if (it == deck.end()) {
            if (error) {
                *error = "读入的手牌里 " + TileName(tile, false) + " 超过 4 张。";
            }
            return false;
        }
        deck.erase(it);
    }
    for (auto& hand : hands_) hand.clear();
    for (auto& pile : discards_) pile.clear();
    for (auto& meld : melds_) meld.clear();
    hands_[player] = parsed;
    SortHandLocked(player);

    std::mt19937 rng(esp_random());
    std::shuffle(deck.begin(), deck.end(), rng);
    for (int seat = 0; seat < 4; ++seat) {
        if (seat == player) continue;
        for (int i = 0; i < 13; ++i) {
            if (deck.empty()) break;
            hands_[seat].push_back(deck.back());
            deck.pop_back();
        }
        SortHandLocked(seat);
    }
    wall_ = std::move(deck);
    current_player_ = player;
    turn_ = 1;
    last_player_ = -1;
    last_tile_ = -1;
    last_claimed_ = false;
    awaiting_claim_ = false;
    claim_can_chi_ = claim_can_peng_ = claim_can_hu_ = false;
    claim_from_ = -1;
    claim_tile_ = -1;
    winner_ = -1;
    result_.clear();
    active_ = true;
    finished_ = false;
    waiting_for_discard_ = hands_[player].size() > 13;
    ESP_LOGI(TAG, "hand set for seat %d, %d tiles, wall %d", player,
             static_cast<int>(hands_[player].size()), static_cast<int>(wall_.size()));
    return ValidateLocked(error);
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
        if (error) *error = "扑克牌请直接在界面上记录。";
        return false;
    }
    int tile = ParseMahjongTile(raw);
    if (tile < 0) {
        if (error) *error = "不认识的牌：" + raw;
        return false;
    }
    auto& hand = hands_[player];
    auto it = std::find(hand.begin(), hand.end(), tile);
    if (it == hand.end()) {
        if (error) *error = "手牌里没有这张牌：" + raw;
        return false;
    }
    hand.erase(it);
    waiting_for_discard_ = false;
    HandleDiscardLocked(player, tile);
    return ValidateLocked(error);
}

bool LocalGame::DiscardIndex(int player, int index, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return DiscardIndexLocked(player, index, error);
}

bool LocalGame::DiscardIndexLocked(int player, int index, std::string* error) {
    if (!active_ || finished_) {
        if (error) *error = "当前没有进行中的牌局。";
        return false;
    }
    if (player < 0 || player > 3 || player != current_player_) {
        if (error) *error = "还没轮到你出牌。";
        return false;
    }
    if (!waiting_for_discard_) {
        if (error) *error = "请先摸牌/读入手牌，再出牌。";
        return false;
    }
    auto& hand = hands_[player];
    if (index < 0 || index >= static_cast<int>(hand.size())) {
        if (error) *error = "手牌序号无效。";
        return false;
    }
    int tile = hand[index];
    hand.erase(hand.begin() + index);
    waiting_for_discard_ = false;
    HandleDiscardLocked(player, tile);
    return ValidateLocked(error);
}

bool LocalGame::DiscardTile(int player, const std::string& raw, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || finished_) {
        if (error) *error = "当前没有进行中的牌局。";
        return false;
    }
    if (player < 0 || player > 3 || player != current_player_) {
        if (error) *error = "还没轮到你出牌。";
        return false;
    }
    int tile = ParseMahjongTile(raw);
    if (tile < 0) {
        if (error) *error = "不认识的牌：" + raw;
        return false;
    }
    auto it = std::find(hands_[player].begin(), hands_[player].end(), tile);
    if (it == hands_[player].end()) {
        if (error) *error = "手里没有这张牌：" + raw;
        return false;
    }
    int index = static_cast<int>(it - hands_[player].begin());
    return DiscardIndexLocked(player, index, error);
}

bool LocalGame::Claim(int player, const std::string& kind, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return ClaimLocked(player, kind, error);
}

bool LocalGame::ClaimLocked(int player, const std::string& kind, std::string* error) {
    if (!active_ || finished_ || type_ != GameType::kMahjong) {
        if (error) *error = "当前没有可吃碰的麻将牌局。";
        return false;
    }
    if (player < 0 || player > 3 || player != my_seat_ || !awaiting_claim_) {
        if (error) *error = "现在没有可吃碰的牌。";
        return false;
    }
    if (kind == "hu" || kind == "胡" || kind == "胡牌") {
        if (!claim_can_hu_) {
            if (error) *error = "这张牌不能胡。";
            return false;
        }
        int tile = claim_tile_;
        awaiting_claim_ = false;
        return HuLocked(player, tile, false, error);
    }
    if (kind == "pass" || kind == "过" || kind == "不要") {
        PassClaimLocked();
        return true;
    }
    int claim_kind = 1;
    if (kind == "chi" || kind == "吃" || kind == "吃牌") claim_kind = 0;
    else if (kind == "peng" || kind == "碰" || kind == "碰牌") claim_kind = 1;
    else {
        if (error) *error = "只支持吃、碰、胡或过。";
        return false;
    }
    if (claim_kind == 0 && !claim_can_chi_) {
        if (error) *error = "手牌不能吃这张牌。";
        return false;
    }
    if (claim_kind == 1 && !claim_can_peng_) {
        if (error) *error = "手牌不够碰。";
        return false;
    }
    if (!CanClaimLocked(player, claim_kind, claim_tile_)) {
        if (error) *error = claim_kind == 1 ? "手牌不够碰。" : "手牌不能吃这张牌。";
        return false;
    }
    if (!ApplyClaimLocked(player, claim_kind, claim_from_, claim_tile_)) {
        if (error) *error = "吃碰失败。";
        return false;
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

void LocalGame::BuildMeldsLocked(int player, PlayerView* out) const {
    out->melds.clear();
    out->meld_count = static_cast<int>(melds_[player].size());
    for (const auto& meld : melds_[player]) {
        std::string text = meld.kind == 1 ? "碰 " : "吃 ";
        for (int i = 0; i < 3; ++i) {
            if (i > 0) text += "-";
            text += TileName(meld.tiles[i], false);
        }
        out->melds.push_back(text);
    }
}

GameView LocalGame::ViewLocked(int seat) const {
    GameView view;
    view.active = active_;
    view.finished = finished_;
    view.demo = demo_;
    view.type = type_;
    view.my_seat = seat;
    view.current_player = current_player_;
    view.current_name = SeatName(current_player_);
    view.wall_remaining = static_cast<int>(wall_.size());
    view.turn = turn_;
    view.winner = winner_;
    view.result = result_;
    view.my_turn = active_ && current_player_ == seat;
    view.awaiting_discard = waiting_for_discard_;
    view.can_discard = view.my_turn && !finished_ && waiting_for_discard_ && !awaiting_claim_;
    view.can_hu = active_ && !finished_ && current_player_ == seat && waiting_for_discard_ &&
                  CanWinLocked(seat);
    view.awaiting_claim = awaiting_claim_;
    view.can_chi = awaiting_claim_ && claim_from_ >= 0 && seat == my_seat_ && claim_can_chi_;
    view.can_peng = awaiting_claim_ && claim_from_ >= 0 && seat == my_seat_ && claim_can_peng_;
    view.claim_can_hu = awaiting_claim_ && claim_from_ >= 0 && seat == my_seat_ && claim_can_hu_;
    view.claim_from = claim_from_;
    if (claim_tile_ >= 0) {
        view.claim_tile = TileName(claim_tile_, type_ == GameType::kPoker);
    }
    view.last_claimed = last_claimed_;
    view.last_discard_player = last_player_;
    if (last_tile_ >= 0) {
        view.last_discard = TileName(last_tile_, type_ == GameType::kPoker);
    }
    for (int i = 0; i < 4; ++i) {
        view.players[i].name = SeatName(i);
        view.players[i].hand_count = static_cast<int>(hands_[i].size());
        view.players[i].discards.clear();
        view.players[i].hand.clear();
        for (int tile : hands_[i]) {
            view.players[i].hand.push_back(TileName(tile, type_ == GameType::kPoker));
        }
        for (int tile : discards_[i]) {
            view.players[i].discards.push_back(TileName(tile, type_ == GameType::kPoker));
        }
        BuildMeldsLocked(i, &view.players[i]);
    }
    if (seat >= 0 && seat <= 3) {
        view.my_hand = view.players[seat].hand;
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
        advice.action = winner_ >= 0 ? "本局已胡" : "本局已结束";
        advice.reason = result_.empty() ? "点击“菜单”重新开始。" : result_;
        return advice;
    }
    if (seat < 0 || seat > 3 || hands_[seat].empty()) {
        advice.action = "等待手牌";
        advice.reason = "还没有读到你的手牌。";
        return advice;
    }
    if (seat == my_seat_ && awaiting_claim_) {
        advice.action = "可以" + std::string(claim_can_hu_ ? "胡" : "") +
                        (claim_can_peng_ ? "碰" : "") + (claim_can_chi_ ? "吃" : "");
        advice.reason = "别家打出 " + TileName(claim_tile_, false) + "，请选择吃/碰/胡/过。";
        advice.meta = "点下方按钮；不操作时牌局会暂停。";
        return advice;
    }
    if (seat == my_seat_ && waiting_for_discard_ && CanWinLocked(seat)) {
        advice.action = "可以胡";
        advice.reason = "当前手牌已经成胡，点“胡”结束本局，或继续打一张。";
        advice.meta = "自摸/荣和按牌型自动判断";
        return advice;
    }

    int index = -1;
    int shanten = 0;
    if (type_ == GameType::kPoker) {
        index = BestPokerDiscard(hands_[seat], nullptr);
    } else {
        index = BestMahjongDiscard(hands_[seat],
                                   static_cast<int>(melds_[seat].size()), &shanten);
    }
    if (index < 0 || index >= static_cast<int>(hands_[seat].size())) {
        advice.action = "暂无建议";
        return advice;
    }
    advice.discard_index = index;
    int tile = hands_[seat][index];
    std::string name = TileName(tile, type_ == GameType::kPoker);
    advice.action = "建议打 " + name;

    if (type_ == GameType::kPoker) {
        std::vector<int> remain = hands_[seat];
        remain.erase(remain.begin() + index);
        int ranks[13] = {0};
        int suits[4] = {0};
        for (int card : remain) {
            ranks[card % 13]++;
            suits[card / 13]++;
        }
        int pairs = 0;
        int trips = 0;
        for (int count : ranks) {
            if (count == 2) pairs++;
            if (count == 3) trips++;
        }
        int flush = *std::max_element(suits, suits + 4);
        advice.reason = "按成牌潜力保留对子/三条/同花/顺子，打掉最难成牌的一张。";
        advice.meta = "5 张牌 · 对子 " + std::to_string(pairs) + " · 三条 " +
                      std::to_string(trips) + " · 同花 " + std::to_string(flush);
        return advice;
    }

    std::vector<int> remain = hands_[seat];
    remain.erase(remain.begin() + index);
    int counts[kMahjongKinds];
    CountHand(remain, counts);
    int ukeire = 0;
    for (int t = 0; t < kMahjongKinds; ++t) {
        int remain_visible = RemainingVisibleLocked(t);
        if (remain_visible <= 0) continue;
        counts[t]++;
        int next = ShantenFromCounts(counts, static_cast<int>(melds_[seat].size()));
        counts[t]--;
        if (next < shanten) {
            ukeire += remain_visible;
        }
    }
    if (shanten <= 0) {
        advice.reason = "已经听牌：这张牌让向听数最低，优先保留进张最多的形状。";
    } else if (shanten <= 2) {
        advice.reason = "接近听牌：打掉孤立牌，保留两面搭子和对子。";
    } else {
        advice.reason = "向听偏远：先处理孤张，保留同一花色的连续牌。";
    }
    advice.meta = "向听 " + std::to_string(shanten) + " · 进张约 " +
                  std::to_string(ukeire) + " 张（未见）" +
                  (melds_[seat].empty() ? "" : " · 已副露 " +
                       std::to_string(melds_[seat].size()));
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
        if (!melds_[seat].empty()) {
            text += "已副露：";
            for (const auto& meld : melds_[seat]) {
                text += meld.kind == 1 ? "碰" : "吃";
                text += Join({meld.tiles[0], meld.tiles[1], meld.tiles[2]}, 3, false);
                text += "；";
            }
        }
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
