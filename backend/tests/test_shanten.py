from app.shanten import analyze_hand, best_discards, shanten, ukeire
from app.tiles import hand_counts, normalize_tile, notation_to_tiles, tile_name


def test_notation():
    tiles = notation_to_tiles("123W 456B 789P EA CE PLUM")
    assert tiles == ["1W", "2W", "3W", "4B", "5B", "6B", "7P", "8P", "9P", "EA", "CE", "PLUM"]
    assert tile_name("1B") == "一条"
    assert tile_name("EA") == "东"
    assert tile_name("PLUM") == "梅"
    assert normalize_tile("红中") == "CE"
    assert normalize_tile("幺鸡") == "1B"
    assert normalize_tile("一饼") == "1P"


def test_winning_hand():
    hand = notation_to_tiles("123W456W789W123B44B")
    assert len(hand) == 14
    assert shanten(hand) == -1


def test_tenpai_and_ukeire():
    hand = notation_to_tiles("123W456W789W123B4B")
    assert shanten(hand) == 0
    total, detail = ukeire(hand_counts(hand))
    assert total > 0
    assert any(d["tile"] == "4B" for d in detail)


def test_seven_pairs():
    hand = notation_to_tiles("11W22W33W44W55W66W7W")
    assert shanten(hand) == 0
    assert shanten(notation_to_tiles("11W22W33W44W55W66W77W")) == -1


def test_best_discard():
    hand = notation_to_tiles("123W456W789W123B45B")
    cands = best_discards(hand, limit=3)
    assert cands
    assert cands[0]["shanten"] == 0
    assert cands[0]["tile"] in hand


def test_empty_hand():
    assert shanten([]) == 99
    assert analyze_hand([])["count"] == 0
