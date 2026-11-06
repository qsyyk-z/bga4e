"""麻将牌局状态机（事件溯源）。

事件类型：
  draw      摸牌      player, tile
  discard   出牌      player, tile
  peng      碰        player, tile
  chi       吃        player, tile, tiles=[两张手牌]
  gang      杠        player, tile
  win       胡        player, tile(可选)
  pass      过
  scan      仅记录识别结果，不改牌局（用于摄像头）
  set_hand  录入手牌  player, tiles=[...]
  score     记分      player, delta

设计目标：
  * 摄像头只看到“打出的牌”时，可用 strict_hands=False / strict_turn=False 记录；
  * 手动录入手牌后，后端可计算向听/进张并给建议；
  * 所有状态都可由事件重放，支持撤销与复盘。
"""
from __future__ import annotations

import copy
import hashlib
import json
import random
from typing import Any

from . import shanten
from .tiles import (
    SUIT_NAMES,
    TILE_ORDER,
    hand_counts,
    is_flower,
    index_to_tile,
    normalize_tile,
    sort_tiles,
    tile_name,
)

PLAYER_WINDS = ["EA", "SO", "WE", "NO"]
WIND_NAMES = {"EA": "东", "SO": "南", "WE": "西", "NO": "北"}


class GameError(ValueError):
    """牌局规则错误，API 层转为 400。"""


def _deck() -> list[str]:
    deck = []
    for code in TILE_ORDER[:34]:
        deck.extend([code] * 4)
    return deck


def initial_state(
    players: list[str] | None = None,
    dealer: int = 0,
    deal: bool = True,
    seed: int | None = None,
    strict_hands: bool = True,
    strict_turn: bool = True,
    game_type: str = "mahjong",
) -> dict:
    players = players or ["东家", "南家", "西家", "北家"]
    if len(players) != 4:
        raise GameError("麻将需要 4 位玩家")
    if dealer not in range(4):
        raise GameError("dealer 必须是 0-3")

    deck = _deck()
    rng = random.Random(seed)
    rng.shuffle(deck)

    state: dict[str, Any] = {
        "game_type": game_type,
        "status": "playing",
        "dealer": dealer,
        "current_player": dealer,
        "turn": 0,
        "round_wind": "EA",
        "strict_hands": strict_hands,
        "strict_turn": strict_turn,
        "wall_remaining": 0,
        "players": [],
        "last_discard": None,
        "last_recognition": None,
        "winner": None,
    }

    for i in range(4):
        hand = []
        if deal:
            hand = sort_tiles(deck[i * 13:(i + 1) * 13])
        state["players"].append({
            "seat": i,
            "name": players[i],
            "wind": PLAYER_WINDS[i],
            "hand": hand,
            "hand_source": "dealt" if deal else "unknown",
            "melds": [],
            "discards": [],
            "score": 25000,
        })
    if deal:
        state["wall_remaining"] = 136 - 52
    else:
        state["wall_remaining"] = 136
    return state


def _player(state: dict, player: int) -> dict:
    if player not in range(4):
        raise GameError("player 必须是 0-3")
    return state["players"][player]


def _count_in_hand(hand: list[str], tile: str) -> int:
    return sum(1 for t in hand if t == tile)


def _remove_tiles(hand: list[str], tiles: list[str]) -> None:
    for t in tiles:
        if t not in hand:
            raise GameError(f"手牌中没有 {tile_name(t)}")
        hand.remove(t)


def _next_player(player: int) -> int:
    return (player + 1) % 4


def apply_event(state: dict, event: dict) -> dict:
    """返回新状态，不修改入参。"""
    state = copy.deepcopy(state)
    if state.get("status") == "finished" and event["type"] not in ("score", "scan", "set_hand"):
        raise GameError("牌局已结束")

    etype = event["type"]
    player = int(event.get("player", 0))
    p = _player(state, player)
    tile = normalize_tile(event.get("tile"))
    tiles = [normalize_tile(t) for t in event.get("tiles", []) if normalize_tile(t)]

    if etype == "draw":
        if not tile:
            raise GameError("摸牌需要 tile")
        if state["strict_turn"] and player != state["current_player"]:
            raise GameError("还没轮到该玩家摸牌")
        if state["wall_remaining"] <= 0:
            raise GameError("牌墙已空")
        p["hand"].append(tile)
        p["hand"] = sort_tiles(p["hand"])
        p["hand_source"] = "tracked"
        state["wall_remaining"] -= 1
        state["current_player"] = player
        # 摸牌后，上一张舍牌的吃碰杠窗口关闭
        state["last_discard"] = None

    elif etype == "discard":
        if not tile:
            raise GameError("出牌需要 tile")
        if state["strict_turn"] and player != state["current_player"]:
            raise GameError("还没轮到该玩家出牌")
        if state["strict_hands"]:
            if tile not in p["hand"]:
                raise GameError(f"手牌中没有 {tile_name(tile)}")
            p["hand"].remove(tile)
        else:
            # 摄像头只看到桌面时，允许手牌未跟踪；若恰好有则扣掉
            if tile in p["hand"]:
                p["hand"].remove(tile)
        p["discards"].append(tile)
        state["last_discard"] = {
            "player": player,
            "tile": tile,
            "turn": state["turn"] + 1,
        }
        state["turn"] += 1
        state["current_player"] = _next_player(player)

    elif etype in ("peng", "chi", "gang"):
        if not tile:
            raise GameError(f"{etype} 需要 tile")
        last = state.get("last_discard")
        if etype in ("peng", "chi") and last and last["player"] == player:
            raise GameError("不能吃/碰自己打出的牌")
        if etype == "peng":
            if state["strict_hands"] and _count_in_hand(p["hand"], tile) < 2:
                raise GameError(f"手牌中没有两张 {tile_name(tile)}")
            _remove_tiles(p["hand"], [tile, tile]) if _count_in_hand(p["hand"], tile) >= 2 else None
            meld_tiles = [tile, tile, tile]
        elif etype == "gang":
            # 大明杠（碰别人的舍牌）手里 3 张；暗杠/加杠手里 4 张
            need = 3 if (last and last.get("tile") == tile) else 4
            if _count_in_hand(p["hand"], tile) < need:
                if state["strict_hands"]:
                    raise GameError(f"手牌中没有足够的 {tile_name(tile)} 用于杠")
                need = min(need, _count_in_hand(p["hand"], tile))
            if need:
                _remove_tiles(p["hand"], [tile] * need)
            meld_tiles = [tile] * 4
        else:  # chi
            if len(tiles) != 2:
                raise GameError("吃需要 tiles 提供两张手牌")
            all_tiles = sort_tiles([tile] + tiles)
            idxs = [TILE_ORDER.index(t) for t in all_tiles]
            if not (len(set(t[1] for t in all_tiles)) == 1 and idxs[2] - idxs[0] == 2 and len(set(idxs)) == 3):
                raise GameError("吃牌必须是同花色的连续三张")
            if state["strict_hands"]:
                _remove_tiles(p["hand"], tiles)
            else:
                for t in tiles:
                    if t in p["hand"]:
                        p["hand"].remove(t)
            meld_tiles = all_tiles
        p["melds"].append({
            "type": etype,
            "tiles": meld_tiles,
            "from": last["player"] if last else None,
        })
        # 被吃/碰/杠的牌从对方弃牌河移走
        if last:
            dp = state["players"][last["player"]]
            if tile in dp.get("discards", []):
                dp["discards"].remove(tile)
        state["last_discard"] = None
        state["current_player"] = player

    elif etype == "win":
        winning = tile or (state.get("last_discard") or {}).get("tile")
        if winning and winning not in p["hand"]:
            p["hand"].append(winning)
        p["hand"] = sort_tiles(p["hand"])
        state["status"] = "finished"
        state["winner"] = player
        state["winning_tile"] = winning
        state["win_type"] = "discard" if state.get("last_discard") else "self_draw"

    elif etype == "pass":
        pass

    elif etype == "scan":
        if not tile:
            raise GameError("scan 需要 tile")
        state["last_recognition"] = {
            "player": player,
            "tile": tile,
            "tile_name": tile_name(tile),
            "confidence": event.get("confidence"),
            "top3": event.get("top3", []),
            "source": event.get("source", "esp32"),
            "ts": event.get("ts"),
        }

    elif etype == "set_hand":
        if not tiles:
            raise GameError("set_hand 需要 tiles")
        p["hand"] = sort_tiles(tiles)
        p["hand_source"] = "manual"

    elif etype == "score":
        delta = int(event.get("delta", 0))
        p["score"] += delta

    else:
        raise GameError(f"未知事件类型: {etype}")

    # 花牌不参与手牌结构，但保留在手里用于展示
    return state


def visible_counts(state: dict) -> list[int]:
    """所有玩家可见的牌（弃牌 + 副露），不含手牌。"""
    counts = [0] * 34
    for p in state["players"]:
        for t in p.get("discards", []):
            t = normalize_tile(t)
            if t in TILE_ORDER[:34]:
                counts[TILE_ORDER.index(t)] += 1
        for meld in p.get("melds", []):
            for t in meld.get("tiles", []):
                t = normalize_tile(t)
                if t in TILE_ORDER[:34]:
                    counts[TILE_ORDER.index(t)] += 1
    return counts


def legal_actions(state: dict, player: int) -> list[dict]:
    if state.get("status") != "playing":
        return []
    p = _player(state, player)
    hand = p["hand"]
    actions: list[dict] = []
    last = state.get("last_discard")
    is_current = player == state.get("current_player")

    # 已经摸牌（14/11/8/5/2 张）：直接出牌/自摸
    if is_current and (len(hand) % 3 == 2 or not state.get("strict_hands")):
        actions.append({"type": "discard", "label": "出牌"})
        if shanten.shanten(hand) <= -1:
            actions.append({"type": "win", "label": "自摸"})
        return actions

    # 别家刚打出牌：可以吃碰杠胡，也可以过；轮到自己时过完还要摸牌
    if last and last["player"] != player:
        t = last["tile"]
        if _count_in_hand(hand, t) >= 2:
            actions.append({"type": "peng", "tile": t, "label": f"碰 {tile_name(t)}"})
        if _count_in_hand(hand, t) >= 3:
            actions.append({"type": "gang", "tile": t, "label": f"杠 {tile_name(t)}"})
        for a in TILE_ORDER[:27]:
            if a[1] != t[1]:
                continue
            combos = _chi_combos(t, a, hand)
            if combos:
                actions.append({"type": "chi", "tile": t, "tiles": combos[0],
                                "label": f"吃 {tile_name(t)}"})
                break
        if shanten.shanten(hand + [t]) <= -1:
            actions.append({"type": "win", "tile": t, "label": "胡"})
        if is_current:
            actions.append({"type": "draw", "label": "摸牌"})
        actions.append({"type": "pass", "label": "过"})
        return actions

    if is_current:
        actions.append({"type": "draw", "label": "摸牌"})
    return actions


def _chi_combos(tile: str, _other: str, hand: list[str]) -> list[list[str]]:
    """返回能跟 tile 组成顺子的两张手牌组合。"""
    if len(tile) != 2 or tile[1] not in SUIT_NAMES:
        return []
    suit = tile[1]
    n = int(tile[0])
    combos = []
    for a, b in ((n - 2, n - 1), (n - 1, n + 1), (n + 1, n + 2)):
        if 1 <= a <= 9 and 1 <= b <= 9:
            ta, tb = f"{a}{suit}", f"{b}{suit}"
            if ta in hand and tb in hand and (a != b or hand.count(ta) >= 2):
                combos.append([ta, tb])
    return combos


def state_hash(state: dict) -> str:
    raw = json.dumps(state, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()[:16]


def hand_summary(state: dict, player: int) -> dict:
    p = _player(state, player)
    open_melds = len(p.get("melds", []))
    info = shanten.analyze_hand(p.get("hand", []), visible_counts(state), open_melds)
    info["player"] = player
    info["name"] = p["name"]
    info["melds"] = p["melds"]
    info["hand_source"] = p.get("hand_source")
    return info


def event_label(event: dict) -> str:
    t = event.get("type")
    p = int(event.get("player", 0))
    name = f"P{p}"
    tile = normalize_tile(event.get("tile"))
    tn = tile_name(tile) if tile else ""
    if t == "draw":
        return f"{name} 摸 {tn}"
    if t == "discard":
        return f"{name} 打 {tn}"
    if t == "peng":
        return f"{name} 碰 {tn}"
    if t == "chi":
        return f"{name} 吃 {tn}"
    if t == "gang":
        return f"{name} 杠 {tn}"
    if t == "win":
        return f"{name} 胡 {tn}"
    if t == "pass":
        return f"{name} 过"
    if t == "scan":
        return f"{name} 识别 {tn}"
    if t == "set_hand":
        return f"{name} 录入手牌"
    if t == "score":
        return f"{name} 记分 {event.get('delta', 0)}"
    return str(t)


def game_summary(row: Any) -> dict:
    state = json.loads(row["state_json"])
    return {
        "id": row["id"],
        "name": row["name"],
        "game_type": row["game_type"],
        "status": row["status"],
        "created_at": row["created_at"],
        "updated_at": row["updated_at"],
        "version": row["version"],
        "current_player": state.get("current_player"),
        "turn": state.get("turn"),
        "wall_remaining": state.get("wall_remaining"),
    }
