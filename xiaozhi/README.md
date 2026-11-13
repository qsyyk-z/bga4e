# 小智 + 4.3C 麻将助手接入

把 BGA4E 后端作为“麻将牌局工具”挂到小智的 MCP 上，并在 **ESP32-S3-Touch-LCD-4.3C（800×480）** 上用 LVGL 显示牌局辅助界面。

适用工程：

```text
D:\M08\AiXiaoZhi_Hiwonder_Sensor
```

- xiaoZhi `PROJECT_VER 1.8.9`
- `main/idf_component.yml` 要求 ESP-IDF >= 5.4.0
- 已用 **ESP-IDF 5.4.4** 编译通过

## 1. 功能

```text
用户说话
  → 小智唤醒 + ASR
  → 小智云端 LLM
      ├─ 调用 MCP 工具：get_state / get_advice / set_hand / record_discard
      ↓ HTTP
  → BGA4E FastAPI 后端（SQLite + 向听/进张）
  → 工具返回中文
  → 小智 LLM 组织语言 + TTS 播报
```

4.3C 屏幕显示：

- 当前玩家、巡目、牌墙
- 四家弃牌
- 最近出牌
- 我的 13/14 张手牌
- 策略建议：打哪张、理由、向听/进张
- 顶部按钮：刷新 / 建议 / 语音
- BOOT 键或“语音”/“牌局”按钮切换牌桌和对话页

## 2. 文件清单

需要拷贝到小智工程：

```text
main/boards/common/mahjong_mcp.cc
main/boards/common/mahjong_mcp.h
main/boards/common/mahjong_ui.cc
main/boards/common/mahjong_ui.h

main/boards/waveshare-s3-touch-lcd-4.3c/config.h
main/boards/waveshare-s3-touch-lcd-4.3c/config.json
main/boards/waveshare-s3-touch-lcd-4.3c/esp32_s3_touch_lcd_4.3c.cc

components/custom_io_expander_ch32v003/
```

`main/CMakeLists.txt` 已经用 `file(GLOB boards/common/*.cc)` 收集 common 源文件，所以 `mahjong_mcp.cc` 和 `mahjong_ui.cc` **不需要手动加 CMake**。

## 3. 板级配置

### 3.1 Kconfig

`main/Kconfig.projbuild` 的 board choice 中加入：

```kconfig
config BOARD_TYPE_ESP32S3_Touch_LCD_4_3C
    bool "Waveshare ESP32-S3-Touch-LCD-4.3C"
    depends on IDF_TARGET_ESP32S3
```

并在文件末尾加入：

```kconfig
menu "BGA4E Mahjong Assistant"
    config BGA4E_BACKEND_URL
        string "BGA4E backend base URL"
        default "http://192.168.1.100:8000"

    config BGA4E_PLAYER
        int "BGA4E player index (0-3)"
        range 0 3
        default 0
endmenu
```

### 3.2 CMake

`main/CMakeLists.txt` 的 board 映射中加入：

```cmake
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C)
    set(BOARD_TYPE "waveshare-s3-touch-lcd-4.3c")
```

### 3.3 分区表

4.3C 固件包含语音模型和 LVGL，6M app 分区只剩 2%：

```text
partitions/v1/16m.csv
```

改成双 7M：

```csv
ota_0,    app,  ota_0,   0x100000,  7M,
ota_1,    app,  ota_1,   0x800000,  7M,
```

当前实测：

```text
AiXiaoZhi_Hiwonder.bin = 0x5dcdf0
app partition 0x700000
16% free
```

### 3.4 编译配置

如果 PowerShell 里提示 `idf.py` 不是可执行命令，用工程里的辅助脚本（会自动设置 ESP-IDF 5.4.4 环境）：

```powershell
cd D:\M08\AiXiaoZhi_Hiwonder_Sensor
.\build_43c.ps1 --version
.\build_43c.ps1 fullclean
.\build_43c.ps1 menuconfig
.\build_43c.ps1 build
.\build_43c.ps1 -p COMx flash monitor
```

也可以先激活 ESP-IDF 环境（PowerShell 必须用点号 `.`，不能只用 `&`）：

```powershell
. e:\esp\v5.4.4\esp-idf\export.ps1
idf.py menuconfig
idf.py build
```

或在 CMD 里：

```bat
e:\esp\v5.4.4\esp-idf\export.bat
cd /d D:\M08\AiXiaoZhi_Hiwonder_Sensor
idf.py fullclean
idf.py build
```

菜单里选择：

```text
Board Type -> Waveshare ESP32-S3-Touch-LCD-4.3C
BGA4E Mahjong Assistant -> 后端 IP 和玩家座位
```

当前 `sdkconfig` 已切换为：

```text
CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C=y
CONFIG_BGA4E_BACKEND_URL="http://192.168.1.100:8000"
CONFIG_BGA4E_PLAYER=0
```

烧录：

```bash
idf.py -p COMx flash monitor
```

## 4. 启动后端

```powershell
cd M08\bga4e\backend
.\.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

网页里先新建一局，或点“生成演示牌局”。

确保：

- 小智和 PC 在同一局域网；
- Windows 防火墙放行 8000；
- `BGA4E_BACKEND_URL` 用 PC 局域网 IP，不要用 `127.0.0.1`。

## 5. MCP 工具

| 工具 | 作用 | 用户说法 |
| --- | --- | --- |
| `self.mahjong.get_state` | 获取牌局、手牌、弃牌、牌墙 | “现在牌局怎么样？” |
| `self.mahjong.get_advice` | 打哪张、理由、向听 | “我该打哪张？” |
| `self.mahjong.set_hand` | 录入手牌 | “我的手牌是……” |
| `self.mahjong.record_discard` | 记录打出的牌 | “记一下，我打三万” |

`get_advice` 固定 `use_llm=false`：后端算牌，小智 LLM 负责说话。

## 6. LVGL 界面

`mahjong_ui.cc` 创建 800×480 的独立 LVGL screen：

```text
┌──────────────────────────────────────────────┐
│ BGA4E 麻将助手   已连接      [刷新][建议][语音] │
├──────────────────────────────┬───────────────┤
│ 当前：西家 · 第 8 巡 · 牌墙 42 │ 策略建议       │
│                              │ 建议打 三万     │
│ 各家弃牌：                    │ 理由……         │
│ 东家：…  南家：…              │ 向听 1 · 8 张   │
│ 西家：…  北家：…              │               │
│ 最近出牌：五万                 │               │
├──────────────────────────────┴───────────────┤
│ 手牌  [一万][二万][三万] …                    │
└──────────────────────────────────────────────┘
```

- 每 1.5 秒检查一次刷新/建议请求；
- 点击“刷新”拉取 `/api/current/summary`；
- 点击“建议”拉取 `/api/current/advice`；
- BOOT 键、屏幕“语音”/“牌局”按钮都能切换聊天页和牌局页；
- 中文使用 `font_puhui_20_4`/`font_puhui_30_4`。

## 7. 注意

- 4.3C 的 RGB 屏、GT911 触摸、CH32V003 IO 扩展、ES7210/ES8311 都按微雪官方板级代码适配；如果实际排线/版本不同，先核对原理图。
- 当前只做了 **编译验证**，还没有在真实 4.3C 板子上烧录验证触摸方向、背光、音频。
- 摄像头识别仍由 BGA4E 的 CardNet 或摄像头节点负责；这一版负责“大屏 + 语音 + LLM 建议”。
- 后端 `current` 接口以 `updated_at` 最新一局为准；多局同时开时建议先固定一局。
