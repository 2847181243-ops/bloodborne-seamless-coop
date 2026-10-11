// Kernel 的实现（T-005）。
//
// 六项职责里属于本文件的部分：加载、日志接线、事件总线接线、健康巡检、熔断。
// 业务逻辑一律不在这里（任务书 §3.6 原则 1）。
//
// ── 熔断的判定规则（v1，全部数值待定，见 policy.h）──────────────────────────
//
//   1. init 返回非 Ok   -> initialized 保持 false，该模块**永不**被 tick。
//   2. tick 返回非 Ok   -> 记一次失败；返回 Timeout 时额外记一次超时。
//   3. tick 实际耗时超过 policy.tickTimeoutMs -> 判为超时，记一次失败。
//      注意这是「返回了但太慢」，**不是**「卡死不返回」：卡死时控制权回不到
//      Kernel，进程一起挂住。这一条边界写在 README 里。
//   4. 连续失败达到 policy.failureThreshold -> circuitOpen = true，
//      之后不再调用它的 tick（任务书 §41.1 T-005 验收原文「停止调用它」）。
//   5. 一次成功清零连续失败计数（"连续"而不是"累计"）。
//
// 熔断后不自动恢复（K-3 定为粘性熔断，policy.h 有说明）；
// 恢复手段只有 resetCircuit() 与重启进程。

#include "bbcoop/kernel/kernel.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace bbcoop::kernel {

namespace {

// 计时用单调时钟：它不会因为系统时间被改而回跳，而超时判定依赖时间差。
using SteadyClock = std::chrono::steady_clock;

std::uint64_t elapsedMs(SteadyClock::time_point start) noexcept {
    const auto delta = SteadyClock::now() - start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
    return ms < 0 ? 0 : static_cast<std::uint64_t>(ms);
}

bool isWorse(Health candidate, Health current) noexcept {
    return static_cast<std::int32_t>(candidate) > static_cast<std::int32_t>(current);
}

Health worstOf(Health left, Health right) noexcept {
    return isWorse(left, right) ? left : right;
}

}  // namespace

Kernel::Kernel(KernelPolicy policy) noexcept : m_policy(policy) {
    m_services.setLogger(&m_logger);
    m_events.setLogger(&m_logger);
}

Kernel::~Kernel() {
    shutdownAll();
}

// ── 加载 ────────────────────────────────────────────────────────────────────

Error Kernel::addModule(std::unique_ptr<IModule> module, bool required) noexcept {
    if (!module) {
        return Error::InvalidArgument;
    }
    if (m_moduleCount >= kMaxModules) {
        m_logger.log(LogLevel::Error, "kernel", "module refused: slot table is full");
        return Error::Refused;
    }

    const char* id = module->id();
    if (id == nullptr || id[0] == '\0') {
        m_logger.log(LogLevel::Error, "kernel", "module refused: id() returned null or empty");
        return Error::InvalidArgument;
    }
    if (findModule(id) != nullptr) {
        m_logger.log(LogLevel::Warn, id, "module refused: duplicate module id");
        return Error::Refused;
    }

    Slot& slot = m_slots[m_moduleCount];
    slot.module = std::move(module);
    slot.status.id = id;
    slot.services.bind(m_services, id);
    slot.context.module_id = id;
    slot.context.host = &slot.host;
    slot.host.logger = &m_logger;
    slot.host.services = &slot.services;
    ++m_moduleCount;

    // 接管之后立刻 init：这样「通过 addModule 加入」与「通过 loadTable 加载」
    // 的模块处在同一个状态机上，测试看到的就不是一条特殊路径。
    initSlot(slot, required);
    return Error::Ok;
}

void Kernel::initSlot(Slot& slot, bool required) noexcept {
    slot.status.required = required;
    slot.status.initialized = false;

    // 决策 2 要求调用点包 try/catch。要清楚它拦得住什么：
    // IModule::init 是 noexcept 的，从它内部抛出的异常会在**它的**边界上
    // 直接 std::terminate，这里的 catch 看不到（见 kernel.h 的说明）。
    // 这里真正兜住的是「接口将来放宽 noexcept」与 Kernel 自身代码的异常。
    Error result = Error::Internal;
    try {
        result = slot.module->init(slot.context);
    } catch (...) {
        result = Error::Internal;
        m_logger.log(LogLevel::Error, slot.status.id, "init threw; treated as Internal");
    }

    slot.status.lastError = result;
    if (result == Error::Ok) {
        slot.status.initialized = true;
        slot.status.health = Health::Ok;
        m_logger.log(LogLevel::Info, slot.status.id, "initialized");
        return;
    }

    // 失败只影响本模块：其余模块继续加载（任务书 §41.1 T-005 验收、§3.6 原则 5）。
    slot.status.health = Health::Unavailable;
    slot.status.unavailableReason = UnavailableReason::InitFailed;
    // 骨架模块按任务书 §41.1 T-007 的验收返回 Unavailable，那是预期行为而不是故障，
    // 所以它记 Info；其余错误码记 Warn —— 日志里不应该把「还没实现」当异常报。
    const LogLevel level = result == Error::Unavailable ? LogLevel::Info : LogLevel::Warn;
    m_logger.log(level, slot.status.id, "init failed; module marked Unavailable, others continue");
}

void Kernel::loadTable(const ModuleTable& table) noexcept {
    // 没加载的模块也要记住：自检输出里「某个模块确实被禁用了」必须看得见，
    // 只看 modules 数组无法区分「被关掉」与「构建时漏了」（T-012 的判定依赖这一点）。
    m_skippedIds = table.skippedIds;
    m_skippedReasons = table.skippedReasons;
    m_skippedCount = table.skippedIds != nullptr && table.skippedReasons != nullptr
                         ? table.skippedCount
                         : 0;

    for (std::size_t i = 0; i < table.loadableCount; ++i) {
        const ModuleDescriptor& descriptor = table.loadable[i];
        const bool required = table.requiredFlags != nullptr && table.requiredFlags[i];

        if (descriptor.id == nullptr || descriptor.create == nullptr) {
            m_logger.log(LogLevel::Error, "kernel", "module table entry is incomplete; entry skipped");
            continue;
        }

        IModule* created = nullptr;
        try {
            created = descriptor.create();
        } catch (...) {
            created = nullptr;
        }
        if (created == nullptr) {
            // 创建失败按「本模块不可用」处理，不影响其他模块（冻结接口的约定）。
            m_logger.log(LogLevel::Warn, descriptor.id, "factory returned null; module skipped");
            continue;
        }

        // 注册表里的 ID 与模块自报的 ID 必须一致：表可能是生成的，
        // 名字对不上说明生成物或者工厂函数的命名约定坏了。宁可拒绝这一项并报错，
        // 也不要让「表里写着一个名字、跑起来是另一个」这种事静默通过。
        const char* createdId = created->id();
        if (createdId == nullptr || std::strcmp(createdId, descriptor.id) != 0) {
            m_logger.log(LogLevel::Error, descriptor.id,
                         "module id mismatch between the table and the instance; entry refused");
            delete created;
            continue;
        }

        const Error added = addModule(std::unique_ptr<IModule>(created), required);
        if (failed(added)) {
            continue;
        }
    }

    // 进程外模块：没有实例可创建，只有 ID 与通道。
    // required 一律为 true —— 核心模块清单未裁决（01-kernel.md §4），
    // 在裁决之前不自作主张地区分「核心」与「非核心」。
    for (std::size_t i = 0; i < table.remoteCount; ++i) {
        const ModuleTable::RemoteEntry& entry = table.remote[i];
        if (entry.id == nullptr || entry.id[0] == '\0') {
            m_logger.log(LogLevel::Error, "kernel", "remote table entry has no id; entry skipped");
            continue;
        }
        const Error added = addRemote(entry.id, bbcoop::ipc::ChannelId{entry.channel}, true);
        if (failed(added)) {
            continue;
        }
    }
}

// ── 进程外模块（07-kernel-seam §1.1 / §1.2）─────────────────────────────────

void Kernel::setLiveness(bbcoop::ipc::IAuxLiveness* liveness) noexcept {
    m_liveness = liveness;
    // 如果 remote 模块是先登记的、liveness 是后接上的，这里补注册回调 ——
    // 否则「辅助进程死了」这件事只能靠别的路径发现，而 07 §1.2① 要求它是回调。
    if (m_liveness == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.source == ModuleSource::Remote && !slot.channelWatched) {
            registerChannelWatch(slot.status.channel, slot);
        }
    }
}

void Kernel::onChannelClosedThunk(void* user, bbcoop::ipc::ChannelId channel) noexcept {
    if (user == nullptr) {
        return;
    }
    static_cast<Kernel*>(user)->onChannelClosed(channel);
}

void Kernel::registerChannelWatch(bbcoop::ipc::ChannelId channel, Slot& slot) noexcept {
    if (m_liveness == nullptr) {
        return;  // 没有 IPC 层：通道关闭只能由宿主显式调 onChannelClosed 注入
    }
    // 同一条通道只注册一次：一个辅助进程上有多个模块（06 §1），
    // 而 IPC 的 watch 对重复注册返回 InvalidArgument。
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        const Slot& other = m_slots[i];
        if (other.status.source == ModuleSource::Remote && other.status.channel == channel &&
            other.channelWatched) {
            slot.channelWatched = true;  // 复用已有的回调
            return;
        }
    }

    const Error result = m_liveness->watch(channel, &Kernel::onChannelClosedThunk, this);
    if (failed(result)) {
        m_logger.log(LogLevel::Warn, slot.status.id,
                     "could not register the channel-closed callback; the channel can only be "
                     "marked unavailable by an explicit call");
        return;
    }
    slot.channelWatched = true;
}

Error Kernel::addRemote(const char* id, bbcoop::ipc::ChannelId channel, bool required) noexcept {
    if (id == nullptr || id[0] == '\0') {
        return Error::InvalidArgument;
    }
    if (m_moduleCount >= kMaxModules) {
        m_logger.log(LogLevel::Error, "kernel", "remote module refused: slot table is full");
        return Error::Refused;
    }
    if (findModule(id) != nullptr) {
        m_logger.log(LogLevel::Warn, id, "remote module refused: duplicate module id");
        return Error::Refused;
    }

    Slot& slot = m_slots[m_moduleCount];
    slot.status.id = id;
    slot.status.source = ModuleSource::Remote;
    slot.status.channel = channel;
    slot.status.required = required;
    slot.status.health = Health::Unavailable;
    slot.status.initialized = false;
    slot.status.lastError = Error::Unavailable;
    // 尚未就绪：reason 保持 None（07 §1.5 的取值表里，null 就是「没有失败」）。
    slot.status.unavailableReason = UnavailableReason::None;
    // 槽位会被复用（shutdown 之后重新加载），所以每个 per-通道 的标记都要显式清零。
    slot.watchdogReady = false;
    slot.terminateRequested = false;
    slot.channelWatched = false;
    ++m_moduleCount;

    registerChannelWatch(channel, slot);

    m_logger.log(LogLevel::Info, id, "remote module registered; waiting for the aux handshake");
    return Error::Ok;
}

void Kernel::markRemoteReady(bbcoop::ipc::ChannelId channel, std::uint64_t nowMs) noexcept {
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.source != ModuleSource::Remote || slot.status.channel != channel) {
            continue;
        }
        if (slot.status.circuitOpen) {
            continue;  // 已经不可用：不因为迟到的一条 Ready 把它拉回来
        }
        if (!slot.status.initialized) {
            slot.status.initialized = true;
            slot.status.health = Health::Ok;
            slot.status.lastError = Error::Ok;
            slot.status.unavailableReason = UnavailableReason::None;
            m_logger.log(LogLevel::Info, slot.status.id, "remote module ready (aux handshake done)");
        }
        // 看门狗从这里开始「武装」：在这之前没有心跳变化可供比较（见 07 §1.3 第 1 条）。
        slot.watchdog.onReady(nowMs);
        slot.watchdogReady = true;
    }
}

void Kernel::onChannelClosed(bbcoop::ipc::ChannelId channel) noexcept {
    markChannelUnavailable(channel, UnavailableReason::ChannelClosed);
}

void Kernel::markChannelUnavailable(bbcoop::ipc::ChannelId channel,
                                    UnavailableReason reason) noexcept {
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.source != ModuleSource::Remote || slot.status.channel != channel) {
            continue;
        }
        if (slot.status.circuitOpen) {
            // 幂等（07 §1.3 第 2 条）：已经不可用就不再迁移、不再计数、不再记日志。
            // 通道关闭与心跳卡住最终会归约到同一个状态，这正是「路径收敛」。
            continue;
        }
        slot.status.circuitOpen = true;
        slot.status.health = Health::Unavailable;
        slot.status.unavailableReason = reason;
        slot.status.lastError = reason == UnavailableReason::HeartbeatStale ? Error::Timeout
                                                                            : Error::Unavailable;
        ++slot.status.unavailableMarks;
        m_logger.log(LogLevel::Warn, slot.status.id,
                     reason == UnavailableReason::HeartbeatStale
                         ? "module unavailable: heartbeat stalled"
                         : "module unavailable: channel closed");
    }
}

void Kernel::pollRemoteLiveness(std::uint64_t nowMs) noexcept {
    if (m_liveness == nullptr) {
        return;  // 没有 IPC 层：remote 模块照常登记，但不判「卡住」
    }
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.source != ModuleSource::Remote) {
            continue;
        }
        if (slot.status.circuitOpen || !slot.watchdogReady) {
            continue;
        }

        const std::uint64_t counter = m_liveness->heartbeatCounter(slot.status.channel);
        const bbcoop::ipc::WatchdogVerdict verdict = slot.watchdog.poll(nowMs, counter);
        if (verdict != bbcoop::ipc::WatchdogVerdict::HeartbeatStale) {
            continue;
        }

        markChannelUnavailable(slot.status.channel, UnavailableReason::HeartbeatStale);
        if (!slot.terminateRequested) {
            // Kernel 只**请求**终止；执行在 T-021 的平台适配层（K-10）。
            m_liveness->requestTerminate(slot.status.channel);
            slot.terminateRequested = true;
        }
        m_logger.log(LogLevel::Warn, slot.status.id,
                     "heartbeat stalled; termination requested (Kernel does not kill)");
    }
}

// ── 周期调用与熔断 ──────────────────────────────────────────────────────────

void Kernel::recordFailure(Slot& slot, Error error) noexcept {
    slot.status.lastError = error;
    constexpr std::uint32_t kCounterMax = std::numeric_limits<std::uint32_t>::max();
    if (slot.status.consecutiveFailures < kCounterMax) {
        ++slot.status.consecutiveFailures;
    }
    if (slot.status.consecutiveFailures < m_policy.failureThreshold) {
        return;
    }

    slot.status.circuitOpen = true;
    slot.status.health = Health::Unavailable;
    slot.status.consecutiveFailures = m_policy.failureThreshold;
    // 原因要能区分「太慢」与「一直报错」（07-kernel-seam §1.5）。
    slot.status.unavailableReason = (error == Error::Timeout) ? UnavailableReason::TickTimeout
                                                              : UnavailableReason::TickFailures;
    ++slot.status.unavailableMarks;
    m_logger.log(LogLevel::Warn, slot.status.id, "circuit opened; tick will not be called again");
}

void Kernel::runTick(Slot& slot, std::uint64_t nowMs) noexcept {
    ++slot.status.tickCalls;

    const SteadyClock::time_point start = SteadyClock::now();
    Error result = Error::Internal;
    try {
        result = slot.module->tick(nowMs);
    } catch (...) {
        result = Error::Internal;
        m_logger.log(LogLevel::Error, slot.status.id, "tick threw; treated as Internal");
    }
    const std::uint64_t spent = elapsedMs(start);

    if (spent > m_policy.tickTimeoutMs) {
        // 返回了，但超过了阈值：判超时。模块自己返回 Ok 也不能免掉这一条 ——
        // 「tick 超时熔断」的对象是耗时本身（任务书 §41.1 T-005）。
        ++slot.status.tickTimeouts;
        m_logger.log(LogLevel::Warn, slot.status.id, "tick exceeded the timeout; counted as a failure");
        recordFailure(slot, Error::Timeout);
        return;
    }

    if (failed(result)) {
        if (result == Error::Timeout) {
            ++slot.status.tickTimeouts;
        }
        recordFailure(slot, result);
        return;
    }

    slot.status.lastError = Error::Ok;
    slot.status.consecutiveFailures = 0;
}

void Kernel::tick(std::uint64_t nowMs) noexcept {
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.source != ModuleSource::Local) {
            continue;  // 进程外模块没有 IModule 实例，它的存活由心跳判定
        }
        if (!slot.status.initialized || slot.status.circuitOpen) {
            // 未初始化或已熔断的模块不再被调用。这一条就是「故障不传播」的执行点：
            // 一个模块停掉之后，循环继续跑到其余模块。
            continue;
        }
        runTick(slot, nowMs);
    }

    // 进程外模块的心跳判定（读共享内存计数，零 IPC 成本）。
    pollRemoteLiveness(nowMs);

    if (!m_sweepDone || nowMs - m_lastSweepMs >= m_policy.healthSweepPeriodMs) {
        pollHealth();
        m_lastSweepMs = nowMs;
        m_sweepDone = true;
    }
}

// ── 健康 ────────────────────────────────────────────────────────────────────

void Kernel::refreshHealth(Slot& slot) noexcept {
    Health reported = Health::Unavailable;
    if (slot.status.source == ModuleSource::Remote) {
        // 进程外模块没有 health() 可调：它的健康由「IPC 侧报告已就绪 + 心跳在动」决定。
        // 辅助进程内部某个模块自己的健康状态要传回来，需要 IPC 的一条消息（T-021 的
        // CallResult / Ready 都还没有承载它）——**这一条没做**，记在 09 的未落地清单里。
        reported = (slot.status.initialized && !slot.status.circuitOpen) ? Health::Ok
                                                                        : Health::Unavailable;
    } else {
        try {
            reported = slot.module->health();
        } catch (...) {
            reported = Health::Unavailable;
        }
    }

    Health effective = reported;
    if (!slot.status.initialized || slot.status.circuitOpen) {
        // imodule.h 把 Unavailable 定义为「未初始化、已被熔断、或尚未实现」，
        // 所以这两种情况下模块自报的状态不作数，否则自检会显示一个假的 Ok。
        effective = Health::Unavailable;
    }

    if (effective != slot.status.health) {
        const LogLevel level = effective == Health::Ok ? LogLevel::Info : LogLevel::Warn;
        m_logger.log(level, slot.status.id, "health changed");
    }
    slot.status.health = effective;
}

void Kernel::pollHealth() noexcept {
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        refreshHealth(m_slots[i]);
    }
}

HealthSummary Kernel::summarizeHealth() const noexcept {
    HealthSummary summary;
    summary.total = m_moduleCount;
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        const ModuleStatus& status = m_slots[i].status;
        switch (status.health) {
            case Health::Ok: ++summary.ok; break;
            case Health::Degraded: ++summary.degraded; break;
            case Health::Unavailable: ++summary.unavailable; break;
        }
        summary.worst = worstOf(summary.worst, status.health);
    }
    return summary;
}

SelfTestOverall Kernel::computeOverall() const noexcept {
    bool anyModule = false;
    bool anyNotOk = false;

    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        const ModuleStatus& status = m_slots[i].status;
        anyModule = true;
        const bool ok = status.initialized && !status.circuitOpen && status.health == Health::Ok;
        if (!ok) {
            anyNotOk = true;
            if (status.required) {
                // 必需项失败就是 fail：这正是任务书 §41.1 T-014
                // 「required 失败时退出码必须非 0」要判定的东西。
                return SelfTestOverall::Fail;
            }
        }
    }

    if (!anyModule || anyNotOk) {
        // 一个模块都没加载也是 degraded，不是 pass：宿主活着但没有任何能力。
        return SelfTestOverall::Degraded;
    }
    return SelfTestOverall::Pass;
}

SelfTestReport Kernel::selfTestReport() noexcept {
    pollHealth();

    // 快照到连续缓冲区：Slot 里 status 之后还有别的成员，
    // 直接对 &m_slots[0].status 做指针算术是错的。
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        m_reportBuffer[i] = m_slots[i].status;
    }

    SelfTestReport report;
    report.overall = computeOverall();
    report.moduleCount = m_moduleCount;
    report.modules = m_moduleCount == 0 ? nullptr : m_reportBuffer;
    report.skippedIds = m_skippedIds;
    report.skippedReasons = m_skippedReasons;
    report.skippedCount = m_skippedCount;
    return report;
}

const ModuleStatus* Kernel::moduleStatusAt(std::size_t index) const noexcept {
    if (index >= m_moduleCount) {
        return nullptr;
    }
    return &m_slots[index].status;
}

const ModuleStatus* Kernel::findModule(const char* id) const noexcept {
    if (id == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        const char* current = m_slots[i].status.id;
        if (current != nullptr && std::strcmp(current, id) == 0) {
            return &m_slots[i].status;
        }
    }
    return nullptr;
}

Error Kernel::resetCircuit(const char* id) noexcept {
    if (id == nullptr) {
        return Error::InvalidArgument;
    }
    for (std::size_t i = 0; i < m_moduleCount; ++i) {
        Slot& slot = m_slots[i];
        if (slot.status.id == nullptr || std::strcmp(slot.status.id, id) != 0) {
            continue;
        }
        if (!slot.status.circuitOpen) {
            return Error::Refused;
        }
        // 通道关闭 / 心跳停滞**不能**手工恢复：通道那头的进程已经不在了，
        // 把状态改回可用只会得到一个「显示正常、实际没人应答」的模块。
        // 这两条路径的恢复手段是 T-021 / T-013 的重启策略（Kernel 不自动重启）。
        if (slot.status.unavailableReason == UnavailableReason::ChannelClosed ||
            slot.status.unavailableReason == UnavailableReason::HeartbeatStale) {
            m_logger.log(LogLevel::Warn, slot.status.id,
                         "circuit reset refused: the peer is gone; restart is T-021's job");
            return Error::Refused;
        }
        slot.status.circuitOpen = false;
        slot.status.consecutiveFailures = 0;
        slot.status.unavailableReason = UnavailableReason::None;
        refreshHealth(slot);
        m_logger.log(LogLevel::Info, slot.status.id, "circuit reset by hand");
        return Error::Ok;
    }
    return Error::Unavailable;
}

// ── shutdown ────────────────────────────────────────────────────────────────

void Kernel::shutdownAll() noexcept {
    // 后进先出：后加载的模块可能依赖先加载的，先关它更安全。
    for (std::size_t i = m_moduleCount; i > 0; --i) {
        Slot& slot = m_slots[i - 1];
        if (!slot.module) {
            continue;
        }
        try {
            slot.module->shutdown();
        } catch (...) {
            m_logger.log(LogLevel::Error, slot.status.id, "shutdown threw; ignored");
        }
        // 冻结接口要求「shutdown 后 Kernel 撤销该模块的全部发布」。
        const std::size_t removed = m_services.withdrawAllFor(slot.status.id);
        if (removed > 0) {
            m_logger.log(LogLevel::Info, slot.status.id, "published services withdrawn");
        }
        slot.module.reset();
    }
    m_moduleCount = 0;
    m_skippedIds = nullptr;
    m_skippedReasons = nullptr;
    m_skippedCount = 0;
    m_sweepDone = false;
    m_lastSweepMs = 0;
}

std::size_t Kernel::moduleCount() const noexcept {
    return m_moduleCount;
}

KernelLogger& Kernel::logger() noexcept {
    return m_logger;
}

EventBus& Kernel::events() noexcept {
    return m_events;
}

ServiceTable& Kernel::services() noexcept {
    return m_services;
}

const KernelPolicy& Kernel::policy() const noexcept {
    return m_policy;
}

}  // namespace bbcoop::kernel
