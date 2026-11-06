"""麻将向听数与进张（ukeire）计算。

只处理标准 34 种牌（万/条/筒/字），花牌不参与和牌结构。
支持：标准型、七对子、十三幺；支持副露（open melds）。
"""
from __future__ import annotations

from functools import lru_cache
from typing import Iterable

from .tiles import hand_counts, index_to_tile, is_yaochu, normalize_tile

# 13 张手牌的标准向听基准
BASE = 8


def _standard_shanten(counts: tuple[int, ...], open_melds: int = 0) -> int:
    """标准型向听：4 面子 + 1 雀头。"""
    counts = list(counts)

    best = 99

    def dfs(i: int, melds: int, taatsu: int, pair: int) -> None:
        nonlocal best
        while i < 34 and counts[i] == 0:
            i += 1
        if i == 34:
            total_melds = open_melds + melds
            t = min(taatsu, max(0, 4 - total_melds))
            s = BASE - 2 * total_melds - t - pair
            if s < best:
                best = s
            return

        # 简单剪枝：剩余牌全部组成面子也不可能更优
        remaining = sum(counts[i:])
        max_new_melds = remaining // 3
        if BASE - 2 * (open_melds + melds + max_new_melds) >= best:
            return

        # 刻子
        if counts[i] >= 3:
            counts[i] -= 3
            dfs(i, melds + 1, taatsu, pair)
            counts[i] += 3

        # 顺子
        if i < 27 and i % 9 <= 6 and counts[i + 1] > 0 and counts[i + 2] > 0:
            counts[i] -= 1
            counts[i + 1] -= 1
            counts[i + 2] -= 1
            dfs(i, melds + 1, taatsu, pair)
            counts[i] += 1
            counts[i + 1] += 1
            counts[i + 2] += 1

        # 对子：可作雀头，也可作搭子
        if counts[i] >= 2:
            if pair == 0:
                counts[i] -= 2
                dfs(i, melds, taatsu, 1)
                counts[i] += 2
            counts[i] -= 2
            if open_melds + melds + taatsu < 4:
                dfs(i, melds, taatsu + 1, pair)
            counts[i] += 2

        # 两面/嵌张搭子
        if i < 27 and i % 9 <= 7 and counts[i + 1] > 0:
            counts[i] -= 1
            counts[i + 1] -= 1
            if open_melds + melds + taatsu < 4:
                dfs(i, melds, taatsu + 1, pair)
            counts[i] += 1
            counts[i + 1] += 1

        if i < 27 and i % 9 <= 6 and counts[i + 2] > 0:
            counts[i] -= 1
            counts[i + 2] -= 1
            if open_melds + melds + taatsu < 4:
                dfs(i, melds, taatsu + 1, pair)
            counts[i] += 1
            counts[i + 2] += 1

        # 单张跳过
        counts[i] -= 1
        dfs(i, melds, taatsu, pair)
        counts[i] += 1

    dfs(0, 0, 0, 0)
    return best


def _seven_pairs_shanten(counts: list[int], open_melds: int = 0) -> int:
    if open_melds:
        return 99
    pairs = sum(c // 2 for c in counts)
    kinds = sum(1 for c in counts if c > 0)
    s = 6 - pairs
    if kinds < 7:
        s += 7 - kinds
    return s


def _thirteen_orphans_shanten(counts: list[int], open_melds: int = 0) -> int:
    if open_melds:
        return 99
    yaochu_idx = [0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33]
    kinds = sum(1 for i in yaochu_idx if counts[i] > 0)
    pair = 1 if any(counts[i] >= 2 for i in yaochu_idx) else 0
    return 13 - kinds - pair


def shanten(hand: Iterable[str], open_melds: int = 0) -> int:
    counts = hand_counts(hand)
    if sum(counts) == 0:
        return 99
    return min(
        _standard_shanten(tuple(counts), open_melds),
        _seven_pairs_shanten(counts, open_melds),
        _thirteen_orphans_shanten(counts, open_melds),
    )


def _remaining_from_visible(visible: list[int] | None, own_counts: list[int] | None = None) -> list[int]:
    visible = visible or [0] * 34
    own = own_counts or [0] * 34
    return [max(0, 4 - visible[i] - own[i]) for i in range(34)]


def ukeire(counts: list[int], visible: list[int] | None = None, open_melds: int = 0) -> tuple[int, list[dict]]:
    """摸一张能降低向听的牌有多少张。返回 (总张数, 明细)。"""
    current = shanten_from_counts(counts, open_melds)
    remaining = _remaining_from_visible(visible, counts)
    total = 0
    detail = []
    for i in range(34):
        if remaining[i] <= 0:
            continue
        counts[i] += 1
        s = shanten_from_counts(counts, open_melds)
        counts[i] -= 1
        if s < current:
            total += remaining[i]
            detail.append({"tile": index_to_tile(i), "remaining": remaining[i], "shanten": s})
    detail.sort(key=lambda x: (-x["remaining"], x["tile"]))
    return total, detail


def shanten_from_counts(counts: list[int], open_melds: int = 0) -> int:
    if sum(counts) == 0:
        return 99
    return min(
        _standard_shanten(tuple(counts), open_melds),
        _seven_pairs_shanten(counts, open_melds),
        _thirteen_orphans_shanten(counts, open_melds),
    )


def best_discards(hand: Iterable[str], visible: list[int] | None = None,
                  open_melds: int = 0, limit: int = 3) -> list[dict]:
    """14 张手牌的最佳出牌候选；返回 [{tile, shanten, ukeire, detail}]。"""
    tiles = [normalize_tile(t) for t in hand]
    tiles = [t for t in tiles if t]
    counts = hand_counts(tiles)
    if sum(counts) % 3 != 2:
        return []
    results = []
    tried = set()
    for tile in tiles:
        if tile in tried:
            continue
        tried.add(tile)
        counts[hand_index(tile)] -= 1
        s = shanten_from_counts(counts, open_melds)
        u, detail = ukeire(counts, visible, open_melds)
        counts[hand_index(tile)] += 1
        # 向听越低越好；同向听时进张越多越好；孤张/客风略微优先
        danger = (visible or [0] * 34)[hand_index(tile)]
        results.append({
            "tile": tile,
            "shanten": s,
            "ukeire": u,
            "detail": detail[:6],
            "visible": danger,
        })
    results.sort(key=lambda r: (r["shanten"], -r["ukeire"], -r["visible"]))
    return results[:limit]


def hand_index(tile: str) -> int:
    from .tiles import _TILE_INDEX
    return _TILE_INDEX[normalize_tile(tile)]


def analyze_hand(hand: Iterable[str], visible: list[int] | None = None,
                 open_melds: int = 0) -> dict:
    tiles = [normalize_tile(t) for t in hand]
    tiles = [t for t in tiles if t]
    counts = hand_counts(tiles)
    s = shanten_from_counts(counts, open_melds) if tiles else 99
    if tiles and len(tiles) % 3 == 2:
        # 14 张时“再摸一张”没有意义；应看出牌后的 13 张进张
        cands = best_discards(tiles, visible, open_melds, limit=1)
        if cands:
            s = cands[0]["shanten"]
            u = cands[0]["ukeire"]
            detail = cands[0]["detail"]
            return {
                "tiles": tiles,
                "count": len(tiles),
                "shanten": s,
                "ukeire": u,
                "ukeire_detail": detail,
                "best_discard": cands[0]["tile"],
            }
    u, detail = ukeire(counts, visible, open_melds) if tiles else (0, [])
    return {
        "tiles": tiles,
        "count": len(tiles),
        "shanten": s,
        "ukeire": u,
        "ukeire_detail": detail[:12],
    }
