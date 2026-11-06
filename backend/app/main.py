"""FastAPI 入口：牌局、事件、复盘、识别接入、LLM 建议、静态前端。"""
from __future__ import annotations

import asyncio
import json
import random
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from . import advisor, config, db, game
from .schemas import AdviceIn, DemoCreate, EventIn, GameCreate, HandIn, RecognitionIn
from .tiles import TILE_ORDER, all_tiles_meta, normalize_tile, tile_meta

BASE_DIR = Path(__file__).resolve().parent.parent
STATIC_DIR = BASE_DIR / "static"


@asynccontextmanager
async def lifespan(app: FastAPI):
    db.init_db()
    yield


app = FastAPI(title=config.APP_NAME, version=config.APP_VERSION, lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[o.strip() for o in config.CORS_ORIGINS if o.strip()],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


class ConnectionManager:
    def __init__(self) -> None:
        self.active: dict[str, set[WebSocket]] = {}

    async def connect(self, game_id: str, ws: WebSocket) -> None:
        await ws.accept()
        self.active.setdefault(game_id, set()).add(ws)

    def disconnect(self, game_id: str, ws: WebSocket) -> None:
        self.active.get(game_id, set()).discard(ws)

    async def broadcast(self, game_id: str, message: dict) -> None:
        dead = []
        for ws in list(self.active.get(game_id, set())):
            try:
                await ws.send_json(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(game_id, ws)


manager = ConnectionManager()


def _load(game_id: str) -> tuple[Any, dict]:
    row = db.get_game_row(game_id)
    if not row:
        raise HTTPException(status_code=404, detail="牌局不存在")
    return row, json.loads(row["state_json"])


def _normalize_event(event: dict) -> dict:
    e = dict(event)
    if e.get("tile"):
        t = normalize_tile(e["tile"])
        if not t:
            raise HTTPException(status_code=400, detail=f"无法识别的牌: {e['tile']}")
        e["tile"] = t
    if e.get("tiles"):
        tiles = [normalize_tile(t) for t in e["tiles"]]
        if any(t is None for t in tiles):
            raise HTTPException(status_code=400, detail="tiles 中有无法识别的牌")
        e["tiles"] = tiles
    if e.get("top3"):
        top3 = []
        for item in e["top3"]:
            if isinstance(item, dict):
                t = normalize_tile(item.get("tile"))
                if t:
                    top3.append({"tile": t, "confidence": item.get("confidence")})
            elif isinstance(item, (list, tuple)) and len(item) >= 2:
                t = normalize_tile(item[0])
                if t:
                    top3.append({"tile": t, "confidence": item[1]})
        e["top3"] = top3
    return e


def _append(game_id: str, event: dict) -> tuple[dict, dict, int]:
    _, state = _load(game_id)
    event = _normalize_event(event)
    try:
        new_state = game.apply_event(state, event)
    except game.GameError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    seq = db.next_seq(game_id)
    db.insert_event(game_id, seq, event)
    db.update_state(game_id, new_state, seq)
    return new_state, event, seq


def _rebuild(game_id: str) -> dict:
    row, _ = _load(game_id)
    state = json.loads(row["initial_json"])
    events = db.get_events(game_id)
    for ev in events:
        payload = json.loads(ev["payload_json"])
        state = game.apply_event(state, payload)
    db.update_state(game_id, state, len(events))
    return state


def _label(event: dict, state: dict) -> str:
    text = game.event_label(event)
    p = event.get("player")
    if isinstance(p, int) and 0 <= p < 4 and state.get("players"):
        text = text.replace(f"P{p}", state["players"][p].get("name", f"P{p}"), 1)
    return text


def _game_payload(row: Any, state: dict) -> dict:
    return {
        "id": row["id"],
        "name": row["name"],
        "game_type": row["game_type"],
        "status": row["status"],
        "created_at": row["created_at"],
        "updated_at": row["updated_at"],
        "version": row["version"],
        "state": state,
        "legal_actions": game.legal_actions(state, state.get("current_player", 0)),
    }


@app.get("/api/health")
async def health() -> dict:
    return {
        "ok": True,
        "app": config.APP_NAME,
        "version": config.APP_VERSION,
        "llm": bool(config.LLM_API_KEY),
        "llm_model": config.LLM_MODEL if config.LLM_API_KEY else None,
    }


@app.get("/api/meta/tiles")
async def meta_tiles() -> dict:
    return {"tiles": all_tiles_meta(), "order": TILE_ORDER}


@app.get("/api/games")
async def list_games() -> list[dict]:
    return [game.game_summary(row) for row in db.list_games()]


@app.post("/api/games")
async def create_game(body: GameCreate) -> dict:
    game_id = uuid.uuid4().hex[:12]
    try:
        state = game.initial_state(
            players=body.players,
            dealer=body.dealer,
            deal=body.deal,
            seed=body.seed,
            strict_hands=body.strict_hands,
            strict_turn=body.strict_turn,
        )
    except game.GameError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    name = body.name or f"牌局 {game_id[:6]}"
    db.create_game_row(game_id, name, "mahjong", state, state)
    row, _ = _load(game_id)
    return _game_payload(row, state)


@app.post("/api/games/demo")
async def create_demo(body: DemoCreate) -> dict:
    game_id = uuid.uuid4().hex[:12]
    seed = body.seed if body.seed is not None else random.randint(1, 10**9)
    state = game.initial_state(players=["东家", "南家", "西家", "北家"],
                               dealer=0, deal=True, seed=seed)
    db.create_game_row(game_id, f"演示牌局 {game_id[:6]}", "mahjong", state, state)
    rng = random.Random(seed + 1)
    for _ in range(max(1, body.turns)):
        _, st = _load(game_id)
        p = st["current_player"]
        hand = st["players"][p]["hand"]
        if len(hand) % 3 == 0:
            tile = rng.choice(TILE_ORDER[:34])
            st, _, _ = _append(game_id, {"type": "draw", "player": p, "tile": tile,
                                         "source": "demo"})
        else:
            tile = rng.choice(hand)
            st, _, _ = _append(game_id, {"type": "discard", "player": p, "tile": tile,
                                         "source": "demo"})
    row, st = _load(game_id)
    return _game_payload(row, st)


@app.get("/api/games/{game_id}")
async def get_game(game_id: str) -> dict:
    row, state = _load(game_id)
    return _game_payload(row, state)


@app.delete("/api/games/{game_id}")
async def delete_game(game_id: str) -> dict:
    if not db.delete_game(game_id):
        raise HTTPException(status_code=404, detail="牌局不存在")
    await manager.broadcast(game_id, {"type": "deleted", "game_id": game_id})
    return {"ok": True}


@app.get("/api/games/{game_id}/events")
async def get_events(game_id: str, after: int = 0) -> dict:
    _, current_state = _load(game_id)
    rows = db.get_events(game_id)
    events = []
    for r in rows:
        if r["seq"] <= after:
            continue
        payload = json.loads(r["payload_json"])
        events.append({
            "seq": r["seq"],
            "type": r["type"],
            "player": r["player"],
            "tile": r["tile"],
            "source": r["source"],
            "confidence": r["confidence"],
            "created_at": r["created_at"],
            "label": _label(payload, current_state),
            "payload": payload,
        })
    return {"events": events, "count": len(rows)}


@app.post("/api/games/{game_id}/events")
async def post_event(game_id: str, body: EventIn) -> dict:
    event = body.model_dump()
    state, event, seq = _append(game_id, event)
    row, state = _load(game_id)
    payload = _game_payload(row, state)
    payload["event"] = {"seq": seq, **event, "label": _label(event, state)}
    await manager.broadcast(game_id, {"type": "state", "data": payload})
    return payload


@app.post("/api/games/{game_id}/recognitions")
async def post_recognition(game_id: str, body: RecognitionIn) -> dict:
    event = {
        "type": "scan",
        "player": body.player,
        "tile": body.tile,
        "confidence": body.confidence,
        "top3": body.top3,
        "source": body.source,
        "ts": body.ts,
    }
    state, event, seq = _append(game_id, event)
    advice = advisor.rule_advice(state, body.player)
    row, state = _load(game_id)
    payload = _game_payload(row, state)
    payload["event"] = {"seq": seq, **event, "label": _label(event, state)}
    payload["advice"] = advice
    await manager.broadcast(game_id, {"type": "recognition", "data": payload})
    return payload


@app.post("/api/games/{game_id}/hands/{player}")
async def set_hand(game_id: str, player: int, body: HandIn) -> dict:
    if player not in range(4):
        raise HTTPException(status_code=400, detail="player 必须是 0-3")
    event = {"type": "set_hand", "player": player, "tiles": body.tiles,
             "source": body.source}
    state, event, seq = _append(game_id, event)
    row, state = _load(game_id)
    payload = _game_payload(row, state)
    payload["event"] = {"seq": seq, **event, "label": _label(event, state)}
    await manager.broadcast(game_id, {"type": "state", "data": payload})
    return payload


@app.post("/api/games/{game_id}/undo")
async def undo(game_id: str) -> dict:
    _load(game_id)
    if not db.delete_last_event(game_id):
        raise HTTPException(status_code=400, detail="没有可撤销的事件")
    state = _rebuild(game_id)
    row, state = _load(game_id)
    payload = _game_payload(row, state)
    await manager.broadcast(game_id, {"type": "state", "data": payload})
    return payload


@app.get("/api/games/{game_id}/replay")
async def replay(game_id: str) -> dict:
    row, _ = _load(game_id)
    initial = json.loads(row["initial_json"])
    state = initial
    events = db.get_events(game_id)
    snapshots = [{"seq": 0, "label": "开局", "event": None, "state": initial}]
    for ev in events:
        payload = json.loads(ev["payload_json"])
        state = game.apply_event(state, payload)
        snapshots.append({
            "seq": ev["seq"],
            "label": _label(payload, state),
            "event": payload,
            "state": state,
        })
    return {
        "game": {"id": row["id"], "name": row["name"], "created_at": row["created_at"]},
        "snapshots": snapshots,
    }


@app.post("/api/games/{game_id}/advice")
async def advice(game_id: str, body: AdviceIn) -> dict:
    row, state = _load(game_id)
    player = body.player if body.player is not None else state.get("current_player", 0)
    if player not in range(4):
        raise HTTPException(status_code=400, detail="player 必须是 0-3")
    result = await advisor.get_advice(state, player, body.use_llm, game_id)
    return {"game_id": game_id, **result}


@app.get("/api/games/{game_id}/hand/{player}")
async def hand_info(game_id: str, player: int) -> dict:
    _, state = _load(game_id)
    if player not in range(4):
        raise HTTPException(status_code=400, detail="player 必须是 0-3")
    return game.hand_summary(state, player)


@app.websocket("/ws/games/{game_id}")
async def ws_game(ws: WebSocket, game_id: str):
    try:
        row, state = _load(game_id)
    except HTTPException:
        await ws.close(code=1008)
        return
    await manager.connect(game_id, ws)
    try:
        await ws.send_json({"type": "state", "data": _game_payload(row, state)})
        while True:
            try:
                msg = await asyncio.wait_for(ws.receive_text(), timeout=30)
                if msg == "ping":
                    await ws.send_text("pong")
            except asyncio.TimeoutError:
                await ws.send_json({"type": "ping"})
    except WebSocketDisconnect:
        pass
    finally:
        manager.disconnect(game_id, ws)


app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")
