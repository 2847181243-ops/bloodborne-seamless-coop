// T-005 / T-006 测试夹具：Kernel 用的假模块与假 IPC。
// owner: AI-00（T-005）。
//
// 为什么夹具在这里而不是 tests/unit/kernel/：`07-kernel-seam.md` §1.6 要求
// `IAuxLiveness` 的假实现放在 `tests/fixtures/`，好让故障注入（T-013）也能用同一套。
//
// 夹具全部**不分配内存**（noexcept 路径上不能抛）：状态是成员变量，计数用定长类型。

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "bbcoop/ipc/aux_liveness.h"
#include "bbcoop/ipc/protocol.h"
#include "imodule.h"
#include "module_context.h"

namespace bbcoop::fixtures {

// 正常模块：init 成功、tick 成功、健康报 Ok。
class NormalModule final : public IModule {
public:
    explicit NormalModule(const char* moduleId) noexcept : m_id(moduleId) {}

    const char* id() const noexcept override { return m_id; }
    Error init(ModuleContext&) noexcept override {
        ++initCalls;
        return Error::Ok;
    }
    Error tick(std::uint64_t) noexcept override {
        ++tickCalls;
        return Error::Ok;
    }
    void shutdown() noexcept override { ++shutdownCalls; }
    Health health() const noexcept override { return reportedHealth; }

    // 注入点：让模块**自报**一个好状态，验证 Kernel 不被它骗（未初始化/已熔断时必须压成 Unavailable）。
    Health reportedHealth = Health::Ok;
    std::uint32_t initCalls = 0;
    std::uint32_t tickCalls = 0;
    std::uint32_t shutdownCalls = 0;

private:
    const char* m_id = nullptr;
};

// init 失败的模块：验收「init 失败不阻断其他模块」。
class InitFailModule final : public IModule {
public:
    InitFailModule(const char* moduleId, Error result) noexcept : m_id(moduleId), m_error(result) {}

    const char* id() const noexcept override { return m_id; }
    Error init(ModuleContext&) noexcept override {
        ++initCalls;
        return m_error;
    }
    Error tick(std::uint64_t) noexcept override {
        ++tickCalls;  // 不该被调用：init 失败的模块永不 tick
        return Error::Ok;
    }
    void shutdown() noexcept override { ++shutdownCalls; }
    Health health() const noexcept override { return Health::Ok; }  // 故意撒谎

    std::uint32_t initCalls = 0;
    std::uint32_t tickCalls = 0;
    std::uint32_t shutdownCalls = 0;

private:
    const char* m_id = nullptr;
    Error m_error = Error::Internal;
};

// tick 行为的注入模式。
enum class TickMode : std::uint8_t {
    Ok = 0,
    PersistentFailure = 1,   // 每次都返回失败
    SelfReportedTimeout = 2, // 返回 Error::Timeout（模块自己说超时了）
    StallThenOk = 3,         // 前 N 次卡过阈值，之后恢复正常
};

// 可以注入「慢 tick」与「一直失败」的模块。
class TickFixtureModule final : public IModule {
public:
    explicit TickFixtureModule(const char* moduleId) noexcept : m_id(moduleId) {}

    const char* id() const noexcept override { return m_id; }
    Error init(ModuleContext&) noexcept override { return Error::Ok; }
    Error tick(std::uint64_t) noexcept override {
        ++tickCalls;
        // 用 sleep_for 而不是忙等：sleep_for 保证**至少**睡够这么久，
        // 于是「耗时 > 阈值」这条判定是确定的；忙等的实际耗时会随机器与优化变化，
        // 在 CI 上会变成偶发失败。代价是每个慢 tick 花掉几毫秒。
        if (tickCalls <= stallTicks && stallMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stallMs));
        }
        switch (mode) {
            case TickMode::Ok: return Error::Ok;
            case TickMode::PersistentFailure: return Error::Internal;
            case TickMode::SelfReportedTimeout: return Error::Timeout;
            case TickMode::StallThenOk: return Error::Ok;
        }
        return Error::Ok;
    }
    void shutdown() noexcept override { ++shutdownCalls; }
    Health health() const noexcept override { return reportedHealth; }

    TickMode mode = TickMode::Ok;
    std::uint64_t stallTicks = 0;  // 前几次 tick 故意慢
    std::uint64_t stallMs = 0;     // 每次慢 tick 睡多久
    Health reportedHealth = Health::Ok;
    std::uint32_t tickCalls = 0;
    std::uint32_t shutdownCalls = 0;

private:
    const char* m_id = nullptr;
};

// ── IAuxLiveness 的假实现（07-kernel-seam §1.6）──────────────────────────────
//
// 它要能：手工触发「通道关闭」、把心跳计数冻住或推进、记录「请求终止」的次数。
class FakeLiveness final : public bbcoop::ipc::IAuxLiveness {
public:
    static constexpr std::size_t kMaxChannels = 8;

    Error watch(bbcoop::ipc::ChannelId channel, ChannelClosedFn fn, void* user) noexcept override {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                return Error::InvalidArgument;  // 重复注册
            }
        }
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (!m_entries[i].used) {
                m_entries[i].used = true;
                m_entries[i].channel = channel;
                m_entries[i].fn = fn;
                m_entries[i].user = user;
                return Error::Ok;
            }
        }
        return Error::Unavailable;
    }

    std::uint64_t heartbeatCounter(bbcoop::ipc::ChannelId channel) const noexcept override {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                return m_entries[i].heartbeat;
            }
        }
        return 0;
    }

    void requestTerminate(bbcoop::ipc::ChannelId channel) noexcept override {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                ++m_entries[i].terminateRequests;
                return;
            }
        }
    }

    // —— 夹具专用 ——

    void advanceHeartbeat(bbcoop::ipc::ChannelId channel) noexcept {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                ++m_entries[i].heartbeat;
                return;
            }
        }
    }

    // 模拟「辅助进程死了」：触发通道关闭回调（只触发一次，和真实实现一致）。
    std::size_t closeChannel(bbcoop::ipc::ChannelId channel) noexcept {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                if (m_entries[i].closed) {
                    return 0;
                }
                m_entries[i].closed = true;
                if (m_entries[i].fn != nullptr) {
                    m_entries[i].fn(m_entries[i].user, channel);
                }
                return 1;
            }
        }
        return 0;
    }

    std::uint32_t terminateRequests(bbcoop::ipc::ChannelId channel) const noexcept {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (m_entries[i].used && m_entries[i].channel == channel) {
                return m_entries[i].terminateRequests;
            }
        }
        return 0;
    }

private:
    struct Entry {
        bool used = false;
        bool closed = false;
        bbcoop::ipc::ChannelId channel{};
        ChannelClosedFn fn = nullptr;
        void* user = nullptr;
        std::uint64_t heartbeat = 0;
        std::uint32_t terminateRequests = 0;
    };
    Entry m_entries[kMaxChannels];
};

}  // namespace bbcoop::fixtures
