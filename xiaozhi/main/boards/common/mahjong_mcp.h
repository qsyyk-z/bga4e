#pragma once

// 注册 BGA4E 离线牌局 MCP 工具。
// xiaozhi 云端 LLM 可以在用户询问“我该打什么牌/现在牌局怎么样”时调用这些工具。
// 工具直接读写 4.3C 上的 LocalGame，不再请求 PC 后端。
void AddMahjongMcpTools();
