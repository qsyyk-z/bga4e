"""API 请求模型。"""
from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, Field

EventType = Literal[
    "draw", "discard", "peng", "chi", "gang", "win", "pass",
    "scan", "set_hand", "score",
]


class GameCreate(BaseModel):
    name: str | None = None
    players: list[str] = Field(default_factory=lambda: ["东家", "南家", "西家", "北家"])
    dealer: int = 0
    deal: bool = True
    seed: int | None = None
    strict_hands: bool = True
    strict_turn: bool = True


class EventIn(BaseModel):
    type: EventType
    player: int = 0
    tile: str | None = None
    tiles: list[str] = Field(default_factory=list)
    delta: int = 0
    source: str = "manual"
    confidence: float | None = None
    top3: list[dict[str, Any]] = Field(default_factory=list)
    ts: int | None = None


class RecognitionIn(BaseModel):
    player: int = 0
    tile: str
    confidence: float | None = None
    top3: list[dict[str, Any]] = Field(default_factory=list)
    source: str = "esp32"
    ts: int | None = None


class AdviceIn(BaseModel):
    player: int | None = None
    use_llm: bool = True


class HandIn(BaseModel):
    tiles: list[str]
    source: str = "manual"


class DemoCreate(BaseModel):
    seed: int | None = 42
    turns: int = 12
