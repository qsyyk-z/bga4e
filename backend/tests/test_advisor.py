import pytest

from app import advisor, config
from app.game import initial_state
from app.tiles import notation_to_tiles


def make_state():
    st = initial_state(deal=False)
    st["players"][0]["hand"] = notation_to_tiles("123W456W789W123B45B")
    st["players"][0]["hand_source"] = "manual"
    return st


def test_parse_json_fence():
    out = advisor._parse_json('```json\n{"action":"discard","tile":"1W"}\n```')
    assert out["tile"] == "1W"


@pytest.mark.asyncio
async def test_llm_advice(monkeypatch):
    monkeypatch.setattr(config, "LLM_API_KEY", "test")
    monkeypatch.setattr(config, "LLM_MODEL", "test-model")

    async def fake_call(state, player, rule):
        return {
            "action": "discard",
            "tile": "1W",
            "reason": "先打孤张一万。",
            "confidence": 0.8,
            "alternatives": [],
        }

    monkeypatch.setattr(advisor, "_call_llm", fake_call)
    out = await advisor.get_advice(make_state(), 0, use_llm=True)
    assert out["provider"] == "llm:test-model"
    assert out["tile"] == "1W"
    assert out["action"] == "discard"


@pytest.mark.asyncio
async def test_llm_fallback_on_error(monkeypatch):
    monkeypatch.setattr(config, "LLM_API_KEY", "test")

    async def boom(state, player, rule):
        raise RuntimeError("network down")

    monkeypatch.setattr(advisor, "_call_llm", boom)
    out = await advisor.get_advice(make_state(), 0, use_llm=True)
    assert out["provider"] == "rule"
    assert "network down" in out["llm_error"]
