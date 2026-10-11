// Kernel 的事件总线（T-005 产出）。
//
// ── 语义：任务书没有规定，属 T-005 的决定 ──────────────────────────────────
//
// 任务书 §3.6 原则 1 只把「事件总线」列为 Kernel 职责，语义未写；
// docs/architecture/01-kernel.md 记为待定项 K-8。v1 定为：
//
//   * 同步投递：publish 内部直接调用订阅者，不排队、不切线程。Kernel 的主循环
//     是单线程的，异步投递会引入一个必须自己解决的并发问题。
//   * 单线程：不加锁。跨线程使用属于未定义行为，需要时先解决主循环模型。
//   * 不允许重入：在处理器内部再 publish 会返回 Refused。重入会让投递顺序取决于
//     调用栈，且订阅者可能观察到「一半的事件」。
//   * 处理器不抛异常：投递点用 try/catch 兜住，异常按「该处理器失败」处理并记录，
//     不影响其他订阅者。处理器签名刻意**不写** noexcept，就是为了让这个捕获有效。
//   * 订阅数上限 kMaxEventSubscriptions，满了返回 Refused 并记日志。
//
// ── 一处必须记录的缺口 ─────────────────────────────────────────────────────
//
// 冻结接口里**没有**给模块的事件入口：src/interfaces/module_context.h 只有
// ILogger 与 IServiceRegistry，没有 subscribe/publish。所以这个总线当前只对
// Kernel 与宿主可用，模块还用不上它。要在 v2 开放，必须按任务书 §52-c 的流程
// 走冻结接口变更，T-005 不擅自改 src/interfaces/。

#pragma once

#include <cstddef>
#include <cstdint>

#include "errors.h"
#include "module_context.h"
#include "policy.h"

namespace bbcoop::kernel {

// 投递给订阅者的事件。payload 的含义由 topic 约定，总线不解释它。
struct Event {
    const char* topic = nullptr;
    const void* payload = nullptr;
    std::uint64_t nowMs = 0;
};

// 处理器：函数指针 + 用户数据。
// 不用 std::function —— 它的构造可能分配内存并抛异常，而这里是一条 noexcept 边界。
// 处理器必须自己保证不抛；万一抛了，总线会捕获并记日志（见 .cpp 的投递循环）。
using EventHandler = void (*)(void* user, const Event& event);

class EventBus final {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    ~EventBus() = default;

    // 订阅一个 topic。topic 用内容比较（strcmp），因此字符串内容相同即可。
    // 同一个 (topic, handler, user) 重复订阅返回 Refused（避免同一条事件被投两次）。
    Error subscribe(const char* topic, EventHandler handler, void* user) noexcept;

    // 取消订阅。没订阅过返回 Refused —— 静默成功会让「取消没生效」看不出来。
    Error unsubscribe(const char* topic, EventHandler handler, void* user) noexcept;

    // 同步投递。返回 Internal 表示**至少一个处理器失败**；其余处理器仍然收到了事件
    // （故障不传播）。payload 由调用方持有，只在本次投递期间有效。
    Error publish(const char* topic, const void* payload, std::uint64_t nowMs) noexcept;

    std::size_t subscriberCount() const noexcept;
    std::size_t publishCount() const noexcept;
    std::size_t reentrantRejections() const noexcept;
    std::size_t handlerFailures() const noexcept;

    // 记录日志用的注入点（Kernel 把自己的 logger 传进来；为空则不记）。
    void setLogger(ILogger* logger) noexcept;

private:
    struct Subscription {
        const char* topic = nullptr;
        EventHandler handler = nullptr;
        void* user = nullptr;
        bool used = false;
    };

    Subscription m_subs[kMaxEventSubscriptions];
    std::size_t m_subscriberCount = 0;
    std::size_t m_publishCount = 0;
    std::size_t m_reentrantRejections = 0;
    std::size_t m_handlerFailures = 0;
    bool m_dispatching = false;
    ILogger* m_logger = nullptr;
};

}  // namespace bbcoop::kernel
