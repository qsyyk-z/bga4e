"""麻将牌的编码、显示名与排序。

牌代码沿用固件 `card_labels.hpp` 的 ASCII 代码：
  万：1W..9W    条：1B..9B    筒：1P..9P
  字：EA 东, SO 南, WE 西, NO 北, CE 中, GR 发, WH 白
  花：PLUM 梅, SPRING 春, ORCHID 兰, SUMMER 夏,
      CHRYS 菊, AUTUMN 秋, BAMBOO 竹, WINTER 冬
"""
from __future__ import annotations

from collections import Counter
from typing import Iterable

SUITS = ("W", "B", "P")
SUIT_NAMES = {"W": "万", "B": "条", "P": "筒"}
SUIT_COLORS = {"W": "red", "B": "green", "P": "blue"}

HONORS = {
    "EA": "东",
    "SO": "南",
    "WE": "西",
    "NO": "北",
    "CE": "中",
    "GR": "发",
    "WH": "白",
}

FLOWERS = {
    "PLUM": "梅",
    "SPRING": "春",
    "ORCHID": "兰",
    "SUMMER": "夏",
    "CHRYS": "菊",
    "AUTUMN": "秋",
    "BAMBOO": "竹",
    "WINTER": "冬",
}

# 别名：接受固件短码、英文、中文序号等
ALIASES = {
    "1T": "1B", "2T": "2B", "3T": "3B", "4T": "4B", "5T": "5B",
    "6T": "6B", "7T": "7B", "8T": "8B", "9T": "9B",
    "E": "EA", "S": "SO", "W": "WE", "N": "NO",
    "C": "CE", "F": "GR", "B": "WH",
    "PL": "PLUM", "SP": "SPRING", "OR": "ORCHID", "SU": "SUMMER",
    "CH": "CHRYS", "AU": "AUTUMN", "BA": "BAMBOO", "WI": "WINTER",
    "梅": "PLUM", "春": "SPRING", "兰": "ORCHID", "夏": "SUMMER",
    "菊": "CHRYS", "秋": "AUTUMN", "竹": "BAMBOO", "冬": "WINTER",
    "东": "EA", "南": "SO", "西": "WE", "北": "NO",
    "中": "CE", "发": "GR", "白": "WH",
}

NUM_CN = "一二三四五六七八九"

TILE_ORDER: list[str] = []
for _suit in SUITS:
    TILE_ORDER += [f"{i}{_suit}" for i in range(1, 10)]
TILE_ORDER += list(HONORS)
TILE_ORDER += list(FLOWERS)

_TILE_INDEX = {code: i for i, code in enumerate(TILE_ORDER[:34])}


def normalize_tile(code: str | None) -> str | None:
    if code is None:
        return None
    s = str(code).strip().upper().replace(" ", "")
    if not s:
        return None
    s = ALIASES.get(s, s)
    if s in TILE_ORDER:
        return s
    # 允许 "1万" / "一万" / "3条" 这类写法
    if len(s) >= 2 and s[-1] in "万条筒":
        num = s[:-1]
        suit = {"万": "W", "条": "B", "筒": "P"}[s[-1]]
        if num in NUM_CN:
            return f"{NUM_CN.index(num) + 1}{suit}"
        if num.isdigit() and 1 <= int(num) <= 9:
            return f"{int(num)}{suit}"
    return None


def is_flower(code: str) -> bool:
    return code in FLOWERS


def is_honor(code: str) -> bool:
    return code in HONORS


def is_terminal(code: str) -> bool:
    return len(code) == 2 and code[0] in "19" and code[1] in "WBP"


def is_yaochu(code: str) -> bool:
    return is_terminal(code) or is_honor(code)


def tile_name(code: str) -> str:
    code = normalize_tile(code) or code
    if code in HONORS:
        return HONORS[code]
    if code in FLOWERS:
        return FLOWERS[code]
    if len(code) == 2 and code[1] in SUITS and code[0].isdigit():
        return f"{NUM_CN[int(code[0]) - 1]}{SUIT_NAMES[code[1]]}"
    return code


def tile_short(code: str) -> str:
    code = normalize_tile(code) or code
    if code in HONORS:
        return HONORS[code]
    if code in FLOWERS:
        return FLOWERS[code]
    return f"{code[0]}{SUIT_NAMES.get(code[1], code[1])}"


def tile_color(code: str) -> str:
    code = normalize_tile(code) or code
    if len(code) == 2 and code[1] in SUIT_COLORS:
        return SUIT_COLORS[code[1]]
    if code in ("CE", "PLUM", "SPRING"):
        return "red"
    if code in ("GR", "BAMBOO", "WINTER"):
        return "green"
    return "blue"


def tile_meta(code: str) -> dict:
    code = normalize_tile(code) or code
    return {
        "code": code,
        "name": tile_name(code),
        "short": tile_short(code),
        "color": tile_color(code),
        "kind": "flower" if code in FLOWERS else ("honor" if code in HONORS else "suit"),
    }


def all_tiles_meta() -> dict[str, dict]:
    return {code: tile_meta(code) for code in TILE_ORDER}


def sort_tiles(codes: Iterable[str]) -> list[str]:
    valid = [c for c in (normalize_tile(x) for x in codes) if c]
    return sorted(valid, key=lambda c: (_TILE_INDEX.get(c, 99), c))


def hand_counts(codes: Iterable[str]) -> list[int]:
    counts = [0] * 34
    for c in codes:
        c = normalize_tile(c)
        if c in _TILE_INDEX:
            counts[_TILE_INDEX[c]] += 1
    return counts


def index_to_tile(idx: int) -> str:
    return TILE_ORDER[idx]


def notation_to_tiles(notation: str) -> list[str]:
    """解析 '123W 456B 789P EA CE' / '123m456p' / '123万456条' 这类简写。"""
    import re

    result: list[str] = []
    suit_map = {"W": "W", "B": "B", "P": "P", "M": "W", "S": "B", "T": "P",
                "万": "W", "条": "B", "筒": "P"}

    # 先按空白切分；再把“连续数字 + 花色”拆成单张
    for token in notation.replace("，", " ").replace(",", " ").split():
        # 纯牌代码（EA/CE/PLUM 等）
        n = normalize_tile(token)
        if n and not any(ch.isdigit() for ch in token):
            result.append(n)
            continue

        # 连续写法：123W、456条、789P
        pos = 0
        matched = False
        for m in re.finditer(r"(\d+)\s*([WBPwbpMST万条筒])", token):
            matched = True
            suit = suit_map[m.group(2)]
            result.extend(f"{d}{suit}" for d in m.group(1))
            pos = m.end()
        if matched:
            # 剩余部分可能还是单个牌代码
            rest = token[pos:].strip()
            if rest:
                n = normalize_tile(rest)
                if n:
                    result.append(n)
        else:
            n = normalize_tile(token)
            if n:
                result.append(n)
    return result


def tiles_to_notation(codes: Iterable[str]) -> str:
    groups = {s: [] for s in SUITS}
    extras = []
    for c in sort_tiles(codes):
        if len(c) == 2 and c[1] in groups:
            groups[c[1]].append(c[0])
        else:
            extras.append(c)
    return " ".join(f"{''.join(v)}{k}" for k, v in groups.items() if v) + (" " if extras else "") + " ".join(extras)


def counts_to_tiles(counts: list[int]) -> list[str]:
    out = []
    for i, n in enumerate(counts):
        out.extend([index_to_tile(i)] * n)
    return out
