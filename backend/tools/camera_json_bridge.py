#!/usr/bin/env python3
"""Read the camera module's CARD_RESULT JSON lines and archive one file per game.

The camera firmware prints lines such as:

    I (12345) CARD_RESULT: {"mode":"poker","rank":"K","suit":"H","conf":0.93,"box":[10,20,300,300]}

This bridge turns them into:
    data/camera/game_YYYYmmdd_HHMMSS.jsonl   (all accepted cards)
    data/camera/latest.json                  ({"hand":[...]} for the 4.3C)

It is intentionally transport-only: the 4.3C can read a hand snapshot from
/sdcard/bga4e/cards.json (see the "SD读牌" button). Copy latest.json to that
file when you want to hand the camera result to the display.

Usage:
    python backend/tools/camera_json_bridge.py --port COM7 --baud 115200
    python backend/tools/camera_json_bridge.py --port COM7 --min-conf 0.5
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover - dependency hint
    print("缺少 pyserial：pip install pyserial", file=sys.stderr)
    raise

ANSI = re.compile(r"\x1b\[[0-9;]*m")
MARKER = "CARD_RESULT"


def extract_json(line: str) -> dict | None:
    line = ANSI.sub("", line)
    if MARKER in line:
        line = line.split(MARKER, 1)[1]
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:].strip())
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def write_atomic(path: Path, text: str) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def card_key(card: dict) -> str:
    if card.get("mode") == "mahjong":
        return "mahjong:" + str(card.get("name", ""))
    return f"poker:{card.get('rank', '')}{card.get('suit', '')}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="camera USB serial port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out-dir", type=Path, default=Path("data/camera"))
    parser.add_argument("--min-conf", type=float, default=0.45)
    parser.add_argument("--max-cards", type=int, default=14)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    game_path = args.out_dir / f"game_{stamp}.jsonl"
    latest_path = args.out_dir / "latest.json"
    latest_path.write_text('{"hand":[]}', encoding="utf-8")

    print(f"camera bridge: {args.port} @ {args.baud} -> {game_path}")
    hand: list[dict] = []
    last_key = ""
    last_ts = 0.0
    accepted = 0
    with serial.Serial(args.port, args.baud, timeout=1) as ser, game_path.open(
        "a", encoding="utf-8"
    ) as game_file:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            card = extract_json(text)
            if card is None:
                continue
            try:
                conf = float(card.get("conf", 0))
            except (TypeError, ValueError):
                conf = 0.0
            if conf < args.min_conf:
                continue

            key = card_key(card)
            now = time.time()
            # 同一张牌连续被识别时只更新置信度/框，不重复加入手牌。
            if key == last_key and now - last_ts < 2.0 and hand:
                hand[-1] = card
            else:
                hand.append(card)
                if len(hand) > args.max_cards:
                    hand = hand[-args.max_cards:]
            last_key, last_ts = key, now

            card["ts"] = now
            line = json.dumps(card, ensure_ascii=False)
            game_file.write(line + "\n")
            game_file.flush()
            write_atomic(latest_path, json.dumps({"hand": hand}, ensure_ascii=False) + "\n")
            accepted += 1
            print(f"[{accepted}] {line}  (hand={len(hand)})")


if __name__ == "__main__":
    raise SystemExit(main())
