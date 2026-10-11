// T-021 通道测试的入口声明（test_channel.cpp 实现，test_main.cpp 调用）。
// owner: AI-00（T-021）。

#pragma once

namespace bbcoop::test {

// 单元级：线格式 / 代理 / 桩 / 调用分发 / 共享内存通道（同一进程内两侧视图）。
void runChannelTests();

// 跨进程：真实命名映射 + 真实子进程（本可执行文件以 --aux-child 再跑一遍）。
void runCrossProcessChannelTests();

// 子进程模式入口。返回进程退出码。
int runAuxChild(const char* mappingName);

}  // namespace bbcoop::test
