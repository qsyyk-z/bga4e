# 小智（AiXiaoZhi_Hiwonder_Sensor）接入

把 BGA4E 后端作为“麻将牌局工具”挂到小智的 MCP 上。小智负责唤醒、ASR、LLM 对话和 TTS；BGA4E 后端负责牌局状态、向听/进张和策略。

适用工程：

```text
D:\M08\AiXiaoZhi_Hiwonder_Sensor
```

这是 Hiwonder 传感器板的小智工程（`PROJECT_VER 1.8.9`），`main/idf_component.yml` 要求 **ESP-IDF >= 5.4.0**，可直接用 5.4.4 编译。

## 1. 架构

```text
用户说话
  → 小智 ESP32（唤醒 + ASR）
  → 小智云端 LLM
      ├─ 需要牌局/建议时，调用设备 MCP 工具
      │    self.mahjong.get_state
      │    self.mahjong.get_advice
      │    self.mahjong.set_hand
      │    self.mahjong.record_discard
      ↓ HTTP
  → BGA4E FastAPI 后端（SQLite + 向听/进张）
  → 工具返回中文文本
  → 小智 LLM 组织语言 + TTS 播报
```

后端只做“牌局事实和建议”，不抢小智的对话。`get_advice` 固定 `use_llm=false`，由小智的 LLM 负责把结果说自然，避免重复调用大模型。

## 2. 拷贝文件

把本目录的：

```text
main/boards/common/mahjong_mcp.cc
main/boards/common/mahjong_mcp.h
```

复制到：

```text
D:\M08\AiXiaoZhi_Hiwonder_Sensor\main\boards\common\mahjong_mcp.cc
D:\M08\AiXiaoZhi_Hiwonder_Sensor\main\boards\common\mahjong_mcp.h
```

这个工程的 `main/CMakeLists.txt` 已经用：

```cmake
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
```

自动收集 `boards/common/*.cc`，所以**不需要改 CMakeLists**。

## 3. 修改 `main/application.cc`

顶部 include：

```cpp
#include "mcp_server.h"
#include "mahjong_mcp.h"   // 新增
```

在 `Application::Initialize()` 末尾的：

```cpp
    McpServer::GetInstance().AddCommonTools();
```

后面加：

```cpp
    AddMahjongMcpTools();   // 新增
```

## 4. 修改 `main/Kconfig.projbuild`

在文件末尾追加：

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

然后：

```bash
idf.py menuconfig
# BGA4E Mahjong Assistant
#   BGA4E_BACKEND_URL = http://<PC 局域网 IP>:8000
#   BGA4E_PLAYER      = 0..3
```

## 5. 编译

```bash
cd D:\M08\AiXiaoZhi_Hiwonder_Sensor
idf.py build
idf.py -p COMx flash monitor
```

> 已在 **ESP-IDF 5.4.4** 上实测编译通过：`AiXiaoZhi_Hiwonder.bin` 大小 `0x3bf530`，app 分区剩余 38%。

工程当前的 `sdkconfig` 已经是：

```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_BOARD_TYPE_HiwonderExploit_S3=y
```

如果之前用别的 IDF 版本配置过 `build/`，先：

```bash
idf.py fullclean
idf.py build
```

## 6. 后端准备

在 PC 上启动 BGA4E：

```powershell
cd M08\bga4e\backend
.\.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

网页里先创建牌局，或点“生成演示牌局”。小智设备访问 `/api/current/...`，会自动使用**最近更新的一局**。

确保：

- 小智和 PC 在同一局域网；
- Windows 防火墙放行 8000；
- `BGA4E_BACKEND_URL` 用 PC 的局域网 IP，不要用 `127.0.0.1`。

## 7. 可以这样说

| 用户说法 | 小智调用的工具 |
| --- | --- |
| “现在牌局怎么样？” | `self.mahjong.get_state` |
| “我该打哪张？” | `self.mahjong.get_advice` |
| “我的手牌是一万、二万、三万……” | `self.mahjong.set_hand` |
| “记一下，我打三万” | `self.mahjong.record_discard` |

`set_hand` 的 `tiles` 参数支持牌代码，也支持后端的中文别名：

```text
万 1W-9W，条 1B-9B，筒 1P-9P
东 EA、南 SO、西 WE、北 NO、中 CE、发 GR、白 WH
```

## 8. 工具返回示例

`get_advice` 返回类似：

```text
建议打三万。打完后 1 向听，还有 8 张进张。当前 1 向听，进张约 8 张。
```

小智的 LLM 可以继续用更口语的方式播报。

## 9. 注意

- 工具通过 `Board::GetInstance().GetNetwork()->CreateHttp()` 访问后端，需要设备已经联网；
- 单次 HTTP 超时 10 秒，失败时返回“暂时连不上牌局服务”，不会让小智卡死；
- 后端 `current` 接口以 `updated_at` 最新的一局为准；如果同时开多局，建议先固定一局（后续可加 `BGA4E_GAME_ID` 配置）；
- 这套接入只负责“LLM/语音策略”，不改变小智的摄像头识别；牌面识别仍由 BGA4E 的 CardNet 或摄像头节点上报。
