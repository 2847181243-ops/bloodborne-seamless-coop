// T-021：ITransport / ISession 的游戏进程侧代理（08-ipc.md D-4）。
// owner: AI-00（T-021）。
//
// 调用方拿到的仍是冻结接口指针；代理负责：
//   * 查询走本地缓存（D-2）——不产生跨边界调用；
//   * send 只把调用交给通道，立刻返回（不等待远端，D-13）。

#pragma once

#include <cstddef>
#include <cstdint>

#include "bbcoop/ipc/caches.h"
#include "bbcoop/ipc/channel.h"
#include "isession.h"
#include "itransport.h"

namespace bbcoop::ipc {

class TransportProxy final : public ITransport {
public:
    TransportProxy(ICallChannel& channel, const TransportCache& cache) noexcept
        : m_channel(channel), m_cache(cache) {}

    bool connected() const noexcept override {
        return m_cache.connected.load(std::memory_order_acquire);
    }

    Error send(const std::uint8_t* data, std::size_t size) noexcept override;

    std::uint32_t rtt_ms() const noexcept override {
        return m_cache.rttMs.load(std::memory_order_acquire);
    }

private:
    ICallChannel& m_channel;
    const TransportCache& m_cache;
};

class SessionProxy final : public ISession {
public:
    explicit SessionProxy(const SessionCache& cache) noexcept : m_cache(cache) {}

    bool active() const noexcept override {
        return m_cache.active.load(std::memory_order_acquire);
    }

    std::uint32_t member_count() const noexcept override {
        return m_cache.memberCount.load(std::memory_order_acquire);
    }

    bool is_host() const noexcept override {
        return m_cache.isHost.load(std::memory_order_acquire);
    }

private:
    const SessionCache& m_cache;
};

}  // namespace bbcoop::ipc
