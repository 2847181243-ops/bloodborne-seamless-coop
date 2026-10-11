// EventBus 的实现（T-005）。
//
// 语义写在 include/bbcoop/kernel/event_bus.h 里；这里只记两处实现上的选择：
//
//   1. 投递前先把匹配的订阅复制到本地数组再调用。原因：处理器里可能调用
//      subscribe / unsubscribe，直接遍历成员数组会让迭代失效。
//      代价是「投递过程中新增的订阅从下一个事件起才生效」—— 这条已写进头文件。
//   2. 处理器调用点包 try/catch。处理器签名刻意不写 noexcept，就是为了让这个捕获
//      有效（见头文件）。一个处理器失败不影响其他处理器，也不影响发布方，
//      但 publish 会返回 Internal，让失败不被静默吞掉。

#include "bbcoop/kernel/event_bus.h"

#include <cstring>

namespace bbcoop::kernel {

namespace {

// 投递用的本地快照项。Subscription 是 EventBus 的私有嵌套类型，
// 这里用一份等价的最小结构，只带投递需要的东西。
struct PendingCall {
    EventHandler handler = nullptr;
    void* user = nullptr;
};

}  // namespace

Error EventBus::subscribe(const char* topic, EventHandler handler, void* user) noexcept {
    if (topic == nullptr || topic[0] == '\0' || handler == nullptr) {
        return Error::InvalidArgument;
    }

    for (std::size_t i = 0; i < kMaxEventSubscriptions; ++i) {
        const Subscription& sub = m_subs[i];
        if (!sub.used) {
            continue;
        }
        if (sub.handler == handler && sub.user == user && std::strcmp(sub.topic, topic) == 0) {
            if (m_logger != nullptr) {
                m_logger->log(LogLevel::Warn, "kernel", "event subscribe refused: duplicate subscription");
            }
            return Error::Refused;
        }
    }

    for (std::size_t i = 0; i < kMaxEventSubscriptions; ++i) {
        Subscription& sub = m_subs[i];
        if (sub.used) {
            continue;
        }
        sub.topic = topic;
        sub.handler = handler;
        sub.user = user;
        sub.used = true;
        ++m_subscriberCount;
        return Error::Ok;
    }

    if (m_logger != nullptr) {
        m_logger->log(LogLevel::Warn, "kernel", "event subscribe refused: subscription table is full");
    }
    return Error::Refused;
}

Error EventBus::unsubscribe(const char* topic, EventHandler handler, void* user) noexcept {
    if (topic == nullptr || handler == nullptr) {
        return Error::InvalidArgument;
    }
    for (std::size_t i = 0; i < kMaxEventSubscriptions; ++i) {
        Subscription& sub = m_subs[i];
        if (!sub.used) {
            continue;
        }
        if (sub.handler == handler && sub.user == user && std::strcmp(sub.topic, topic) == 0) {
            sub = Subscription{};
            --m_subscriberCount;
            return Error::Ok;
        }
    }
    return Error::Refused;
}

Error EventBus::publish(const char* topic, const void* payload, std::uint64_t nowMs) noexcept {
    if (topic == nullptr || topic[0] == '\0') {
        return Error::InvalidArgument;
    }
    if (m_dispatching) {
        ++m_reentrantRejections;
        if (m_logger != nullptr) {
            m_logger->log(LogLevel::Warn, "kernel", "event publish refused: reentrant publish");
        }
        return Error::Refused;
    }

    // 先把匹配项复制出来，避免处理器在投递过程中改动订阅表。
    PendingCall matches[kMaxEventSubscriptions];
    std::size_t matchCount = 0;
    for (std::size_t i = 0; i < kMaxEventSubscriptions; ++i) {
        const Subscription& sub = m_subs[i];
        if (sub.used && sub.topic != nullptr && std::strcmp(sub.topic, topic) == 0) {
            matches[matchCount].handler = sub.handler;
            matches[matchCount].user = sub.user;
            ++matchCount;
        }
    }

    ++m_publishCount;
    m_dispatching = true;
    std::size_t failures = 0;
    const Event event{topic, payload, nowMs};
    for (std::size_t i = 0; i < matchCount; ++i) {
        try {
            matches[i].handler(matches[i].user, event);
        } catch (...) {
            // 处理器不得抛异常；万一抛了，只记这一次失败，继续投给其余处理器。
            ++failures;
            ++m_handlerFailures;
        }
    }
    m_dispatching = false;

    if (failures > 0) {
        if (m_logger != nullptr) {
            m_logger->log(LogLevel::Error, "kernel", "event handler threw; event delivered to the rest");
        }
        return Error::Internal;
    }
    return Error::Ok;
}

std::size_t EventBus::subscriberCount() const noexcept {
    return m_subscriberCount;
}

std::size_t EventBus::publishCount() const noexcept {
    return m_publishCount;
}

std::size_t EventBus::reentrantRejections() const noexcept {
    return m_reentrantRejections;
}

std::size_t EventBus::handlerFailures() const noexcept {
    return m_handlerFailures;
}

void EventBus::setLogger(ILogger* logger) noexcept {
    m_logger = logger;
}

}  // namespace bbcoop::kernel
