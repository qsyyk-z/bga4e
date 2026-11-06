"""策略建议：本地向听/进张规则 + OpenAI 兼容 LLM。"""
from __future__ import annotations

import json
import re
import time
from typing import Any

import httpx

from . import config, db, shanten
from .game import legal_actions
from .tiles import normalize_tile, tile_name

SYSTEM_PROMPT = """你是“BGA4E”桌面游戏助手，服务对象是老年人。
你熟悉中国麻将（标准 4 面子 + 1 将，含碰/吃/杠）。请根据牌局给出一个明确、可执行的建议。
要求：
1. 只输出一个 JSON 对象，不要 Markdown，不要解释 JSON 之外的内容。
2. 字段：action（discard/draw/peng/chi/gang/win/pass）、tile（牌代码，如 3W/5B/EA；非出牌可为空）、reason（不超过 80 个汉字，口语化、温和）、confidence（0~1）、alternatives（最多 2 个，每个含 tile 和 reason）。
3. 优先选择向听数最低、进张最多的打法；同时提醒危险牌。
4. 不确定时给出保守建议，不要编造牌。
"""


def _hand_info(state: dict, player: int) -> dict:
    p = state["players"][player]
    hand = p.get("hand", [])
    open_melds = len(p.get("melds", []))
    visible = shanten_visible(state)
    info = shanten.analyze_hand(hand, visible, open_melds)
    info["player"] = player
    info["name"] = p["name"]
    info["melds"] = p.get("melds", [])
    info["hand_source"] = p.get("hand_source", "unknown")
    return info


def shanten_visible(state: dict) -> list[int]:
    from .game import visible_counts
    return visible_counts(state)


def _state_digest(state: dict, player: int) -> str:
    from .game import state_hash
    return state_hash({"state": state, "player": player})


def rule_advice(state: dict, player: int) -> dict:
    p = state["players"][player]
    hand = p.get("hand", [])
    info = _hand_info(state, player)
    n = len(hand)

    if n == 0:
        return {
            "action": "none",
            "tile": None,
            "reason": "还没有录入手牌。请先手动输入或逐张扫描自己的手牌，我才能计算向听和进张。",
            "confidence": 0.0,
            "provider": "rule",
            "needs_hand": True,
            "shanten": None,
            "ukeire": 0,
            "alternatives": [],
            "analysis": info,
        }

    if info["shanten"] <= -1 and n % 3 == 2:
        return {
            "action": "win",
            "tile": None,
            "reason": "手牌已经和牌，可以直接胡。",
            "confidence": 1.0,
            "provider": "rule",
            "shanten": -1,
            "ukeire": 0,
            "alternatives": [],
            "analysis": info,
        }

    if n % 3 == 2:
        cands = shanten.best_discards(hand, shanten_visible(state), len(p.get("melds", [])), limit=3)
        if not cands:
            return {
                "action": "none",
                "tile": None,
                "reason": "手牌张数不完整，请检查是否录入了 13 或 14 张。",
                "confidence": 0.0,
                "provider": "rule",
                "needs_hand": True,
                "alternatives": [],
                "analysis": info,
            }
        best = cands[0]
        alt = [
            {"tile": c["tile"], "tile_name": tile_name(c["tile"]),
             "reason": f"打 {tile_name(c['tile'])} 后 {c['shanten']} 向听，进张 {c['ukeire']} 张"}
            for c in cands[1:]
        ]
        reason = (
            f"建议打 {tile_name(best['tile'])}：打完后 {best['shanten']} 向听，"
            f"还有 {best['ukeire']} 张进张。"
        )
        if best["shanten"] == 0:
            reason += "已经听牌，注意观察对手舍牌。"
        return {
            "action": "discard",
            "tile": best["tile"],
            "tile_name": tile_name(best["tile"]),
            "reason": reason,
            "confidence": 0.75,
            "provider": "rule",
            "shanten": best["shanten"],
            "ukeire": best["ukeire"],
            "alternatives": alt,
            "analysis": info,
        }

    # 13 张：等待摸牌
    waits = info.get("ukeire_detail", [])
    wait_text = "、".join(f"{tile_name(w['tile'])}({w['remaining']})" for w in waits[:4]) or "暂无"
    return {
        "action": "draw",
        "tile": None,
        "reason": f"现在 {info['shanten']} 向听，等待摸牌。有效进张：{wait_text}。",
        "confidence": 0.7,
        "provider": "rule",
        "shanten": info["shanten"],
        "ukeire": info["ukeire"],
        "alternatives": [],
        "analysis": info,
    }


def _state_for_prompt(state: dict, player: int) -> dict:
    p = state["players"][player]
    info = _hand_info(state, player)
    return {
        "player": player,
        "name": p["name"],
        "hand": hand_to_display(p.get("hand", [])),
        "melds": [
            {"type": m.get("type"), "tiles": hand_to_display(m.get("tiles", []))}
            for m in p.get("melds", [])
        ],
        "discards": hand_to_display(p.get("discards", [])),
        "shanten": info.get("shanten"),
        "ukeire": info.get("ukeire"),
        "wall_remaining": state.get("wall_remaining"),
        "current_player": state.get("current_player"),
        "players": [
            {
                "seat": q["seat"], "name": q["name"], "wind": q["wind"],
                "discards": hand_to_display(q.get("discards", [])),
                "melds": len(q.get("melds", [])),
                "score": q.get("score"),
            }
            for q in state["players"]
        ],
        "last_discard": state.get("last_discard"),
        "last_recognition": state.get("last_recognition"),
    }


def hand_to_display(tiles: list[str]) -> list[dict]:
    return [{"code": t, "name": tile_name(t)} for t in tiles]


async def _call_llm(state: dict, player: int, rule: dict) -> dict:
    summary = _state_for_prompt(state, player)
    user_prompt = (
        "当前牌局（JSON）：\n"
        + json.dumps(summary, ensure_ascii=False)
        + "\n\n本地规则分析：\n"
        + json.dumps({
            "action": rule.get("action"),
            "tile": rule.get("tile"),
            "reason": rule.get("reason"),
            "shanten": rule.get("shanten"),
            "ukeire": rule.get("ukeire"),
            "alternatives": rule.get("alternatives", []),
        }, ensure_ascii=False)
        + "\n\n请给出你的建议，只输出 JSON。"
    )
    payload = {
        "model": config.LLM_MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ],
        "temperature": config.LLM_TEMPERATURE,
        "max_tokens": 500,
    }
    headers = {
        "Authorization": f"Bearer {config.LLM_API_KEY}",
        "Content-Type": "application/json",
    }
    url = config.LLM_BASE_URL
    if not url.endswith("/chat/completions"):
        url = url + "/chat/completions"
    async with httpx.AsyncClient(timeout=config.LLM_TIMEOUT) as client:
        resp = await client.post(url, headers=headers, json=payload)
        resp.raise_for_status()
        data = resp.json()
    content = data["choices"][0]["message"]["content"]
    return _parse_json(content)


def _parse_json(content: str) -> dict:
    text = content.strip()
    text = re.sub(r"^```(?:json)?\s*", "", text, flags=re.I)
    text = re.sub(r"\s*```$", "", text)
    start, end = text.find("{"), text.rfind("}")
    if start < 0 or end < 0:
        raise ValueError("LLM 没有返回 JSON")
    return json.loads(text[start:end + 1])


def _valid_llm_result(result: dict, state: dict, player: int, rule: dict) -> bool:
    action = result.get("action")
    if action not in ("discard", "draw", "peng", "chi", "gang", "win", "pass"):
        return False
    legal = {a["type"] for a in legal_actions(state, player)}
    if legal and action not in legal:
        return False
    if action == "discard":
        tile = normalize_tile(result.get("tile"))
        if not tile:
            return False
        result["tile"] = tile
        hand = state["players"][player].get("hand", [])
        if hand and tile not in hand:
            return False
    if action in ("peng", "chi", "gang"):
        if not result.get("tile"):
            return False
        result["tile"] = normalize_tile(result.get("tile"))
    return True


async def get_advice(state: dict, player: int, use_llm: bool = True,
                     game_id: str | None = None) -> dict:
    t0 = time.time()
    rule = rule_advice(state, player)
    rule["provider"] = "rule"
    rule["latency_ms"] = 0

    if not use_llm or not config.LLM_API_KEY or rule.get("needs_hand"):
        rule["latency_ms"] = int((time.time() - t0) * 1000)
        return rule

    digest = _state_digest(state, player)
    cache_key = f"llm:{config.LLM_MODEL}"
    if game_id:
        cached = db.get_cached_advice(game_id, digest, cache_key)
        if cached:
            cached["cached"] = True
            return cached

    try:
        result = await _call_llm(state, player, rule)
        if not _valid_llm_result(result, state, player, rule):
            raise ValueError("LLM 建议不合法")
        out = {
            "action": result.get("action"),
            "tile": result.get("tile"),
            "tile_name": tile_name(result["tile"]) if result.get("tile") else None,
            "reason": result.get("reason") or rule.get("reason"),
            "confidence": float(result.get("confidence", 0.7)),
            "provider": f"llm:{config.LLM_MODEL}",
            "shanten": rule.get("shanten"),
            "ukeire": rule.get("ukeire"),
            "alternatives": result.get("alternatives") or rule.get("alternatives", []),
            "analysis": rule.get("analysis", {}),
            "latency_ms": int((time.time() - t0) * 1000),
        }
        if game_id:
            db.save_advice(game_id, digest, cache_key, out)
        return out
    except Exception as exc:  # noqa: BLE001
        rule["provider"] = "rule"
        rule["llm_error"] = str(exc)[:200]
        rule["latency_ms"] = int((time.time() - t0) * 1000)
        return rule
