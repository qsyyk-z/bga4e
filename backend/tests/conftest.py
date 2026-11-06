import sys
from pathlib import Path

import pytest

BACKEND = Path(__file__).resolve().parent.parent
if str(BACKEND) not in sys.path:
    sys.path.insert(0, str(BACKEND))


@pytest.fixture()
def client(tmp_path, monkeypatch):
    import app.config as config
    import app.db as db
    from fastapi.testclient import TestClient
    from app.main import app

    db.DB_PATH = tmp_path / "test.db"
    db.init_db()
    monkeypatch.setattr(config, "LLM_API_KEY", "")
    with TestClient(app) as c:
        yield c
