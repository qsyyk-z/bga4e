# 旧目录归档说明

主工程已经统一到：

```text
D:\M08\bga4e
```

这个目录同时包含：

- `backend/`：FastAPI 牌局后端；
- `firmware_app/`：摄像头 ESP-IDF 工程；
- `xiaozhi/`：小智 + 4.3C 显示固件（完整源码）；
- `scripts/`：启动和烧录脚本。

以下目录不再作为主工程，只保留作备份/对照：

| 路径 | 说明 |
| --- | --- |
| `D:\M08\AiXiaoZhi_Hiwonder_Sensor` | 重构前的完整小智工程，最新源码已收进 `xiaozhi/` |
| `D:\大学\制造工程体验\M08\bga4e` | 旧 clone，带未提交的 probe 桥接实验，不再修改 |
| `D:\M08\bga4e` 的旧 `build/` | 已从 Git 索引移除，只在本地保留 |

以后只改主工程，避免三个目录再次分叉。
