"""后端配置：数据库、LLM、CORS。"""
from __future__ import annotations

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = BASE_DIR / "data"
DATA_DIR.mkdir(parents=True, exist_ok=True)

# .env（可选）
try:
    from dotenv import load_dotenv
    load_dotenv(BASE_DIR / ".env")
except Exception:
    pass

DB_PATH = Path(os.getenv("BGA4E_DB", str(DATA_DIR / "bga4e.db")))

# 任何 OpenAI 兼容接口都可以：DeepSeek / Qwen / OpenAI / 本地 vLLM
LLM_API_KEY = os.getenv("LLM_API_KEY") or os.getenv("DEEPSEEK_API_KEY") or ""
LLM_BASE_URL = os.getenv("LLM_BASE_URL", "https://api.deepseek.com/v1").rstrip("/")
LLM_MODEL = os.getenv("LLM_MODEL", "deepseek-chat")
LLM_TIMEOUT = float(os.getenv("LLM_TIMEOUT", "25"))
LLM_TEMPERATURE = float(os.getenv("LLM_TEMPERATURE", "0.2"))

# 识别事件来源允许跨域（ESP32 / 调试页面）
CORS_ORIGINS = os.getenv("BGA4E_CORS", "*").split(",")

APP_NAME = "BGA4E 麻将牌局助手"
APP_VERSION = "1.0.0"
