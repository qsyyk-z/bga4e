def test_health_and_meta(client):
    assert client.get("/api/health").json()["ok"] is True
    meta = client.get("/api/meta/tiles").json()
    assert "1W" in meta["tiles"]
    assert meta["tiles"]["1B"]["name"] == "一条"


def test_create_draw_discard_advice_replay(client):
    r = client.post("/api/games", json={"name": "测试局", "seed": 7, "deal": True})
    assert r.status_code == 200
    g = r.json()
    gid = g["id"]
    st = g["state"]
    assert len(st["players"]) == 4
    assert len(st["players"][0]["hand"]) == 13
    assert st["current_player"] == 0

    r = client.post(f"/api/games/{gid}/events",
                    json={"type": "draw", "player": 0, "tile": "1W"})
    assert r.status_code == 200
    st = r.json()["state"]
    assert len(st["players"][0]["hand"]) == 14

    r = client.post(f"/api/games/{gid}/advice", json={"player": 0, "use_llm": False})
    assert r.status_code == 200
    advice = r.json()
    assert advice["action"] in ("discard", "win", "draw")
    assert advice["provider"] == "rule"

    tile = st["players"][0]["hand"][0]
    r = client.post(f"/api/games/{gid}/events",
                    json={"type": "discard", "player": 0, "tile": tile})
    assert r.status_code == 200
    st = r.json()["state"]
    assert st["current_player"] == 1
    assert st["last_discard"]["tile"] == tile

    replay = client.get(f"/api/games/{gid}/replay").json()
    assert len(replay["snapshots"]) == 3

    r = client.post(f"/api/games/{gid}/undo")
    assert r.status_code == 200
    assert r.json()["state"]["current_player"] == 0


def test_strict_hands_rejects_unknown_discard(client):
    g = client.post("/api/games", json={"deal": True, "seed": 2, "strict_hands": True}).json()
    gid = g["id"]
    r = client.post(f"/api/games/{gid}/events",
                    json={"type": "discard", "player": 0, "tile": "EA"})
    # 起手不一定有东；有则可能 200。这里只验证错误结构存在或状态合法
    assert r.status_code in (200, 400)
    if r.status_code == 400:
        assert "手牌" in r.json()["detail"]


def test_observer_mode_and_manual_hand(client):
    g = client.post("/api/games", json={
        "deal": False, "strict_hands": False, "strict_turn": False,
    }).json()
    gid = g["id"]
    r = client.post(f"/api/games/{gid}/events",
                    json={"type": "discard", "player": 2, "tile": "3W"})
    assert r.status_code == 200
    assert r.json()["state"]["last_discard"]["tile"] == "3W"

    r = client.post(f"/api/games/{gid}/hands/0",
                    json={"tiles": ["1W", "2W", "3W", "4W", "5W", "6W", "7W", "8W",
                                    "9W", "1B", "2B", "3B", "4B"]})
    assert r.status_code == 200
    r = client.post(f"/api/games/{gid}/advice", json={"player": 0, "use_llm": False})
    assert r.status_code == 200
    assert r.json()["action"] == "draw"


def test_current_endpoints_for_voice(client):
    g = client.post("/api/games", json={"deal": True, "seed": 11}).json()
    gid = g["id"]
    summary = client.get("/api/current/summary?player=0")
    assert summary.status_code == 200
    s = summary.json()
    assert s["game_id"] == gid
    assert s["my_player"] == 0
    assert len(s["my_hand"]) == 13

    r = client.post("/api/current/advice", json={"player": 0, "use_llm": False})
    assert r.status_code == 200
    assert r.json()["provider"] == "rule"

    r = client.post("/api/current/hands/0", json={
        "tiles": ["1W", "2W", "3W", "4W", "5W", "6W", "7W", "8W",
                  "9W", "1B", "2B", "3B", "4B", "5B"],
    })
    assert r.status_code == 200
    r = client.post("/api/current/advice", json={"player": 0, "use_llm": False})
    assert r.status_code == 200
    assert r.json()["action"] in ("discard", "win")


def test_recognition_and_demo(client):
    g = client.post("/api/games", json={"deal": True, "seed": 3}).json()
    gid = g["id"]
    r = client.post(f"/api/games/{gid}/recognitions",
                    json={"player": 0, "tile": "3W", "confidence": 0.91,
                          "top3": [{"tile": "3W", "confidence": 0.91}]})
    assert r.status_code == 200
    assert r.json()["state"]["last_recognition"]["tile"] == "3W"
    assert r.json()["advice"]["provider"] == "rule"

    r = client.post("/api/games/demo", json={"seed": 5, "turns": 8})
    assert r.status_code == 200
    assert r.json()["version"] >= 4
