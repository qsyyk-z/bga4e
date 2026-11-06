"""SQLite 存储：牌局、事件、建议缓存。

采用事件溯源：
  games.initial_json 保存开局状态
  events 保存每一步
  games.state_json 保存当前快照（可从事件重放）
"""
from __future__ import annotations

import json
import sqlite3
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import config

DB_PATH: Path = config.DB_PATH


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), timeout=10, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


def init_db() -> None:
    with closing(connect()) as conn:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS games (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                game_type TEXT NOT NULL,
                status TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                version INTEGER NOT NULL DEFAULT 0,
                initial_json TEXT NOT NULL,
                state_json TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
                seq INTEGER NOT NULL,
                type TEXT NOT NULL,
                player INTEGER NOT NULL,
                tile TEXT,
                source TEXT NOT NULL DEFAULT 'manual',
                confidence REAL,
                payload_json TEXT NOT NULL,
                created_at TEXT NOT NULL,
                UNIQUE(game_id, seq)
            );
            CREATE INDEX IF NOT EXISTS idx_events_game_seq ON events(game_id, seq);
            CREATE TABLE IF NOT EXISTS advice_cache (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                game_id TEXT NOT NULL,
                state_hash TEXT NOT NULL,
                provider TEXT NOT NULL,
                response_json TEXT NOT NULL,
                created_at TEXT NOT NULL,
                UNIQUE(game_id, state_hash, provider)
            );
            """
        )
        conn.commit()


def create_game_row(game_id: str, name: str, game_type: str, initial: dict, state: dict) -> None:
    ts = now_iso()
    with closing(connect()) as conn:
        conn.execute(
            "INSERT INTO games(id,name,game_type,status,created_at,updated_at,version,initial_json,state_json) "
            "VALUES(?,?,?,?,?,?,?,?,?)",
            (game_id, name, game_type, state.get("status", "playing"), ts, ts, 0,
             json.dumps(initial, ensure_ascii=False), json.dumps(state, ensure_ascii=False)),
        )
        conn.commit()


def get_game_row(game_id: str) -> sqlite3.Row | None:
    with closing(connect()) as conn:
        return conn.execute("SELECT * FROM games WHERE id=?", (game_id,)).fetchone()


def list_games() -> list[sqlite3.Row]:
    with closing(connect()) as conn:
        return conn.execute("SELECT * FROM games ORDER BY updated_at DESC").fetchall()


def delete_game(game_id: str) -> bool:
    with closing(connect()) as conn:
        cur = conn.execute("DELETE FROM games WHERE id=?", (game_id,))
        conn.commit()
        return cur.rowcount > 0


def update_state(game_id: str, state: dict, version: int) -> None:
    with closing(connect()) as conn:
        conn.execute(
            "UPDATE games SET state_json=?, version=?, updated_at=?, status=? WHERE id=?",
            (json.dumps(state, ensure_ascii=False), version, now_iso(),
             state.get("status", "playing"), game_id),
        )
        conn.commit()


def next_seq(game_id: str) -> int:
    with closing(connect()) as conn:
        row = conn.execute("SELECT COALESCE(MAX(seq),0) AS m FROM events WHERE game_id=?", (game_id,)).fetchone()
        return int(row["m"]) + 1


def insert_event(game_id: str, seq: int, event: dict) -> int:
    with closing(connect()) as conn:
        cur = conn.execute(
            "INSERT INTO events(game_id,seq,type,player,tile,source,confidence,payload_json,created_at) "
            "VALUES(?,?,?,?,?,?,?,?,?)",
            (game_id, seq, event["type"], int(event.get("player", 0)),
             event.get("tile"), event.get("source", "manual"),
             event.get("confidence"), json.dumps(event, ensure_ascii=False), now_iso()),
        )
        conn.commit()
        return int(cur.lastrowid)


def get_events(game_id: str) -> list[sqlite3.Row]:
    with closing(connect()) as conn:
        return conn.execute("SELECT * FROM events WHERE game_id=? ORDER BY seq", (game_id,)).fetchall()


def delete_last_event(game_id: str) -> bool:
    with closing(connect()) as conn:
        row = conn.execute("SELECT id FROM events WHERE game_id=? ORDER BY seq DESC LIMIT 1", (game_id,)).fetchone()
        if not row:
            return False
        conn.execute("DELETE FROM events WHERE id=?", (row["id"],))
        conn.commit()
        return True


def get_cached_advice(game_id: str, state_hash: str, provider: str) -> dict | None:
    with closing(connect()) as conn:
        row = conn.execute(
            "SELECT response_json FROM advice_cache WHERE game_id=? AND state_hash=? AND provider=?",
            (game_id, state_hash, provider),
        ).fetchone()
        return json.loads(row["response_json"]) if row else None


def save_advice(game_id: str, state_hash: str, provider: str, response: dict) -> None:
    with closing(connect()) as conn:
        conn.execute(
            "INSERT OR REPLACE INTO advice_cache(game_id,state_hash,provider,response_json,created_at) "
            "VALUES(?,?,?,?,?)",
            (game_id, state_hash, provider, json.dumps(response, ensure_ascii=False), now_iso()),
        )
        conn.commit()
