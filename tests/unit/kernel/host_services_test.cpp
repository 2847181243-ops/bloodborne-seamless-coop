// T-006 单元测试之五：**宿主能力（事件总线与日志）**。
// owner: AI-00（T-006）。
//
// ⚠️ 任务书 §41.1 T-006 要求「4 个单元测试」覆盖加载 / 故障隔离 / 熔断 / 健康聚合。
//    本文件是**第 5 个**，不在那四项里 —— 它覆盖的是任务书 §3.6 原则 1 里 Kernel
//    的另外两项职责：**事件总线**与**日志**。
//    为什么不并进那四个文件：它们是独立的能力，塞进去只会让「4 个用例覆盖 4 个领域」这句话
//    变得名不副实；而删掉它们又是**为了对齐一个数字而丢覆盖率** —— 两者都不取。
//
// 日志这一块的已知缺口（写在测试里，免得被读成「日志已经做完」）：
// K-7 要求的「有界队列 / 单写线程 / 落盘 / 可丢不可丢分类」**尚未实现**
// （09-kernel-host.md §7 与 §10 记着）。这里测的只是已有的那一层：级别过滤、计数、sink 注入。

#include <cstring>
#include <string>

#include "bbcoop/kernel/event_bus.h"
#include "bbcoop/kernel/host_services.h"
#include "bbcoop/kernel/kernel.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::LogLevel;
using bbcoop::kernel::Event;
using bbcoop::kernel::EventBus;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::KernelLogger;

// ── 事件总线 ────────────────────────────────────────────────────────────────

struct Counter {
    std::size_t calls = 0;
    std::string lastTopic;
};

void onEvent(void* user, const Event& event) {
    auto* counter = static_cast<Counter*>(user);
    ++counter->calls;
    if (event.topic != nullptr) {
        counter->lastTopic = event.topic;
    }
}

// 重入：处理器内部再 publish 必须被拒绝，否则会无限递归。
EventBus* g_bus = nullptr;
std::size_t g_reentryResult = 999;

void onReentrantPublish(void*, const Event&) {
    if (g_bus != nullptr) {
        g_reentryResult = static_cast<std::size_t>(g_bus->publish("topic", nullptr, 0));
    }
}

// 同步投递、重复订阅拒绝、重入拒绝并计数、取消订阅的语义。
void testEventBusSemantics() {
    EventBus bus;
    Counter counter;
    CHECK_EQ(static_cast<int>(bus.subscribe("death", onEvent, &counter)),
             static_cast<int>(Error::Ok));

    CHECK_EQ(static_cast<int>(bus.publish("death", nullptr, 42)), static_cast<int>(Error::Ok));
    CHECK_EQ(counter.calls, static_cast<std::size_t>(1));
    CHECK_EQ(counter.lastTopic, std::string("death"));
    CHECK_EQ(bus.publishCount(), static_cast<std::size_t>(1));
    CHECK_EQ(bus.subscriberCount(), static_cast<std::size_t>(1));

    // 同一个 (topic, handler, user) 重复订阅：拒绝，避免同一条事件投两次。
    CHECK_EQ(static_cast<int>(bus.subscribe("death", onEvent, &counter)),
             static_cast<int>(Error::Refused));

    // 重入被拒绝并计数（无穷递归是内核级故障）。
    g_bus = &bus;
    g_reentryResult = 999;
    CHECK_EQ(static_cast<int>(bus.subscribe("reentry", onReentrantPublish, nullptr)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(bus.publish("reentry", nullptr, 1)), static_cast<int>(Error::Ok));
    CHECK_EQ(g_reentryResult, static_cast<std::size_t>(Error::Refused));
    CHECK_EQ(bus.reentrantRejections(), static_cast<std::size_t>(1));
    g_bus = nullptr;

    // 没有订阅者的事件：投递成功，不是错误。
    CHECK_EQ(static_cast<int>(bus.publish("nobody", nullptr, 2)), static_cast<int>(Error::Ok));

    // 取消订阅：没订阅过时返回 Refused，而不是静默成功。
    CHECK_EQ(static_cast<int>(bus.unsubscribe("nobody", onEvent, &counter)),
             static_cast<int>(Error::Refused));
    CHECK_EQ(static_cast<int>(bus.unsubscribe("death", onEvent, &counter)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(bus.subscriberCount(), static_cast<std::size_t>(1));
}

// ── 日志 ────────────────────────────────────────────────────────────────────

struct LogCapture {
    std::size_t lines = 0;
    std::string last;
};

void onLogLine(void* user, const char* line) {
    auto* capture = static_cast<LogCapture*>(user);
    ++capture->lines;
    if (line != nullptr) {
        capture->last = line;
    }
}

// 级别过滤 + 计数 + sink 注入；行里必须带模块 ID 与消息（否则按模块过滤无从谈起）。
void testLoggerFiltersCountsAndCarriesModuleId() {
    Kernel kernel;
    KernelLogger& logger = kernel.logger();

    LogCapture capture;
    logger.setSink(onLogLine, &capture);
    logger.setMinLevel(LogLevel::Warn);

    logger.log(LogLevel::Info, "mod.x", "info is below the threshold");
    CHECK_EQ(capture.lines, static_cast<std::size_t>(0));
    CHECK_EQ(logger.droppedCount(), static_cast<std::uint32_t>(1));

    logger.log(LogLevel::Warn, "mod.x", "warn passes");
    CHECK_EQ(capture.lines, static_cast<std::size_t>(1));
    CHECK_EQ(logger.writtenCount(), static_cast<std::uint32_t>(1));

    CHECK(capture.last.find("mod.x") != std::string::npos);
    CHECK(capture.last.find("warn passes") != std::string::npos);

    // 降门槛之后 Info 也能出来（证明过滤是按当前门槛算的，不是写死的）。
    logger.setMinLevel(LogLevel::Debug);
    logger.log(LogLevel::Info, "mod.y", "now visible");
    CHECK_EQ(capture.lines, static_cast<std::size_t>(2));
    CHECK(capture.last.find("mod.y") != std::string::npos);
}

}  // namespace

int main() {
    testEventBusSemantics();
    testLoggerFiltersCountsAndCarriesModuleId();
    return bbcoop::test::finish();
}
