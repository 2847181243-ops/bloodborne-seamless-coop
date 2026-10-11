// Kernel：模块宿主（T-005 产出）。
//
// 职责（任务书 §3.6 原则 1）：加载、配置、日志、事件总线、健康巡检、熔断。
// **不含业务逻辑**：不实现任何游戏机制、网络协议或密码学，
// 边界清单见 docs/architecture/01-kernel.md。
//
// ── 隔离的边界：进程内拦不住崩溃（不要再往下写「已隔离」）────────────────────
//
// 进程外模块（6 个）由**进程隔离**兜住：辅助进程崩了，游戏进程活着，
// IPC 通道关闭 → 这里的 onChannelClosed() 把相关模块标为不可用。
// 进程内模块（11 个）只有「错误码 + 超时熔断」这一层：
// **段错误与真正卡死不返回的 tick 会带走整个游戏进程**，Kernel 没有补救手段
// （超时是调用返回之后才测出来的，见 kernel.cpp 的说明）。
//
// 任务书 §41.3 全局 Gate 第 9 项写的是「任一非核心模块崩溃 → 进程存活」，
// 它对进程外的 6 个成立，对进程内的 11 个**不成立**。
// **禁止**在任何自检输出或报告里写「Gate 第 9 项已满足」。
//
// ── noexcept 与 try/catch 的真实关系（别把它当成万能兜底）──────────────────
//
// IModule::init / tick 在冻结接口里是 noexcept 的。**一个 noexcept 函数里抛出的
// 异常会在该函数边界直接调用 std::terminate，调用方的 try/catch 看不到它。**
// 所以下面的调用点仍然包 try/catch，但它实际拦住的是：
//   * 未来接口若放宽 noexcept 的情况；
//   * Kernel 自己代码里的异常（例如本文件内部的操作）。
// 真正违反 noexcept 契约的模块仍然会带走进程 —— 这一点在 README 里写明，
// 不假装 try/catch 解决了它。

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "bbcoop/ipc/aux_liveness.h"
#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/watchdog.h"
#include "errors.h"
#include "event_bus.h"
#include "host_services.h"
#include "imodule.h"
#include "module_context.h"
#include "module_table.h"
#include "policy.h"
#include "version.h"

namespace bbcoop::kernel {

// 模块的来源（07-kernel-seam §1.1）。Kernel 对两者的健康巡检与熔断**走同一条
// 代码路径**，唯一差别是：Remote 的「不可用」还可以由外部事件设置
// （通道关闭 / 心跳停滞），而不只能由 tick 返回错误。
enum class ModuleSource : std::uint8_t {
    Local = 0,   // 进程内：Kernel 直接持有 IModule*
    Remote = 1,  // 进程外：没有 IModule 实例，只有一条 IPC 通道
};

inline constexpr const char* to_string(ModuleSource value) noexcept {
    switch (value) {
        case ModuleSource::Local: return "local";
        case ModuleSource::Remote: return "remote";
    }
    return "unknown";
}

// 不可用的原因。**直接复用 IPC 层的枚举**（`bbcoop/ipc/protocol.h`）：
// 自检报告里的取值必须与 08-ipc.md 的措辞逐字一致，两处各写一份就会漂移。
using UnavailableReason = bbcoop::ipc::UnavailableReason;

// 一个模块在 Kernel 里的运行状态。自检报告直接用它。
struct ModuleStatus {
    // 与任务书 §3.6 注册表逐字一致的模块 ID。
    const char* id = nullptr;

    // 进程内还是进程外。
    ModuleSource source = ModuleSource::Local;

    // 已经按 Kernel 的覆盖规则算过（见 kernel.cpp 的 refreshHealth）：
    // 未初始化或已熔断时一定是 Unavailable，不管模块自己的 health() 说什么。
    Health health = Health::Unavailable;

    // init 是否返回过 Ok。熔断之后它保持 true —— 它确实初始化过。
    bool initialized = false;

    // 熔断是否已打开。打开后 Kernel **不再调用**它的 tick。
    bool circuitOpen = false;

    // 是否必需项。必需项失败 → overall 为 fail。
    bool required = false;

    // 最近一次 init 或 tick 的结果。Remote 模块没有 tick，保持 Unavailable。
    Error lastError = Error::Unavailable;

    // 为什么不可用（`07-kernel-seam.md` §1.5 要求自检能区分）。
    // None 表示可用；其余取值与 08-ipc.md 的 UnavailableReason 同源。
    UnavailableReason unavailableReason = UnavailableReason::None;

    // Remote 模块绑定的通道（Local 模块忽略）。
    bbcoop::ipc::ChannelId channel{};

    // Kernel 实际调用 tick 的次数（熔断后不再增长，这条可用来验证「停止调用」）。
    std::uint64_t tickCalls = 0;

    // 被判为超时的次数，以及当前连续失败次数（成功一次即清零）。
    std::uint32_t tickTimeouts = 0;
    std::uint32_t consecutiveFailures = 0;

    // 「被判不可用」的**真实迁移**次数（从可用变不可用才算一次）。
    // 幂等的证明就在这个计数上：重复的通道关闭回调不该让它变大。
    std::uint32_t unavailableMarks = 0;
};

// 健康汇总（T-006 的「健康聚合」用它）。
struct HealthSummary {
    std::size_t total = 0;
    std::size_t ok = 0;
    std::size_t degraded = 0;
    std::size_t unavailable = 0;

    // 最差的一个状态（没有模块时按 Ok 处理）。聚合不能只看计数：
    // 「一个模块 Unavailable」与「五个模块全 Ok」是两种系统。
    Health worst = Health::Ok;
};

// 自检的整体结论。取值与 `--self-test` 的 overall 字段一一对应。
enum class SelfTestOverall : std::int32_t {
    Pass = 0,
    Degraded = 1,
    Fail = 2,
};

inline constexpr const char* to_string(SelfTestOverall value) noexcept {
    switch (value) {
        case SelfTestOverall::Pass: return "pass";
        case SelfTestOverall::Degraded: return "degraded";
        case SelfTestOverall::Fail: return "fail";
    }
    return "fail";
}

// 自检报告。**不含动态内存**：它只指向 Kernel 自己的状态数组与模块表，
// 因此可以在 noexcept 的路径上生成。
//
// modules 指向 Kernel 内部的一份**快照**（按槽位顺序连续存放），
// 下一次 selfTestReport() 会覆盖它 —— 报告不拥有内存，不要跨调用保存。
struct SelfTestReport {
    SelfTestOverall overall = SelfTestOverall::Degraded;

    const ModuleStatus* modules = nullptr;
    std::size_t moduleCount = 0;

    const char* const* skippedIds = nullptr;
    const char* const* skippedReasons = nullptr;
    std::size_t skippedCount = 0;
};

class Kernel final {
public:
    explicit Kernel(KernelPolicy policy = KernelPolicy{}) noexcept;
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;
    ~Kernel();

    // 按模块表加载：每一项都尝试「创建实例 -> init」。
    // **单项失败不影响其他项**（任务书 §41.1 T-005 验收、§3.6 原则 5）：
    // 创建返回 nullptr、init 返回非 Ok、重复 ID、表满，都只影响那一项。
    // 表里的 remote 项走 addRemote()，不创建实例。
    void loadTable(const ModuleTable& table) noexcept;

    // ── 进程外模块（07-kernel-seam §1.1 / §1.2）─────────────────────────────
    //
    // Kernel 只依赖 IAuxLiveness 三件事：通道关闭回调、本地读心跳计数、请求终止。
    // 不传（nullptr）时 remote 模块照常登记，只是不会被判定为「卡住」——
    // 这样没有 IPC 的单元测试也能覆盖登记与通道关闭两条路径。
    void setLiveness(bbcoop::ipc::IAuxLiveness* liveness) noexcept;

    // 登记一个进程外模块：只有 ID 与通道，没有 IModule 实例。
    // 它初始为「未就绪」，直到 IPC 侧报告 Ready（markRemoteReady）。
    Error addRemote(const char* id, bbcoop::ipc::ChannelId channel,
                    bool required = true) noexcept;

    // IPC 侧完成了握手 / 收到 Ready：该通道上的 remote 模块进入可用状态。
    // 同时把心跳看门狗置为 ready —— **从这一刻起**才可能判「卡住」（见 ipc 的
    // ChannelWatchdog：观察到第一次心跳变化之后才武装）。
    void markRemoteReady(bbcoop::ipc::ChannelId channel, std::uint64_t nowMs) noexcept;

    // 通道关闭（辅助进程死了 / 被杀 / 自己退出）→ 该通道上的 remote 模块
    // 全部标为不可用，原因 channel_closed。**幂等**：重复回调不重复计数。
    void onChannelClosed(bbcoop::ipc::ChannelId channel) noexcept;

    // 每 tick 调用：读共享内存里的心跳计数，判定「卡住」。
    // 判定为卡住时：标不可用（heartbeat_stale）并**请求**终止该辅助进程（只发请求）。
    void pollRemoteLiveness(std::uint64_t nowMs) noexcept;

    // 交一个实例给 Kernel：接管所有权 -> init -> 进槽位。
    // 返回值说的是「**注册**这件事有没有成功」（Ok = 已接管），
    // init 的结果要看状态里的 initialized / lastError —— 与配置驱动的路径一致。
    // 失败时不接管，调用方仍然持有这个 unique_ptr。
    //
    // 为什么这条路径存在：故障注入（T-013）与单元测试需要在手上留一个可观察的
    // 模块实例；从配置驱动的路径创建的实例归 Kernel 所有，测试看不到它。
    Error addModule(std::unique_ptr<IModule> module, bool required = false) noexcept;

    // 周期调用：先 tick 所有「已初始化且未熔断」的 **local** 模块，
    // 再判定 remote 模块的心跳，最后按周期做健康巡检。
    void tick(std::uint64_t nowMs) noexcept;

    // 立即巡检一次（忽略周期）：刷新各模块的 health，并在状态变化时记日志。
    void pollHealth() noexcept;

    // 关闭全部已创建的模块（后进先出），并撤销它们发布的服务。
    // 即使某个模块的 init 曾经失败也会调用它的 shutdown ——
    // 冻结接口要求 shutdown 幂等且「即使 init 曾经失败也应能安全调用」。
    void shutdownAll() noexcept;

    std::size_t moduleCount() const noexcept;
    const ModuleStatus* moduleStatusAt(std::size_t index) const noexcept;
    const ModuleStatus* findModule(const char* id) const noexcept;

    HealthSummary summarizeHealth() const noexcept;

    // 生成自检报告。会先做一次健康巡检，保证报告是当下的状态。
    SelfTestReport selfTestReport() noexcept;

    // 手工恢复：把某个模块的熔断清掉，让它重新被 tick。
    // 当前**没有**任何自动恢复路径（K-3 定为粘性熔断），
    // 所以这是重启之外唯一的恢复手段 —— 也正因如此它必须是显式的。
    Error resetCircuit(const char* id) noexcept;

    KernelLogger& logger() noexcept;
    EventBus& events() noexcept;
    ServiceTable& services() noexcept;
    const KernelPolicy& policy() const noexcept;

private:
    // 一个模块的槽位。全部按值存放在固定数组里：没有动态分配，
    // 因此 loadTable / tick 可以在 noexcept 路径上完成。
    struct Slot {
        std::unique_ptr<IModule> module;
        ModuleStatus status;
        ModuleServiceView services;
        ModuleContext context;
        HostServices host;

        // Remote 专用：这条通道的心跳看门狗（T-021 的 ChannelWatchdog，
        // 含「先武装再判卡住」与「只报一次」两条规则）。
        bbcoop::ipc::ChannelWatchdog watchdog;
        bool watchdogReady = false;
        bool terminateRequested = false;

        // 这条通道是否已经向 IPC 层注册过「通道关闭」回调。
        // 一个辅助进程装 6 个远程模块（06 §1），而 watch 对同一条通道**重复注册会
        // 返回 InvalidArgument** —— 所以按通道注册一次，不能每个模块注册一次。
        bool channelWatched = false;
    };

    // 所有模块状态里最差的那个 + required 的失败情况。
    SelfTestOverall computeOverall() const noexcept;
    void refreshHealth(Slot& slot) noexcept;
    void initSlot(Slot& slot, bool required) noexcept;
    void runTick(Slot& slot, std::uint64_t nowMs) noexcept;
    void recordFailure(Slot& slot, Error error) noexcept;

    // 把某条通道上的全部 remote 模块标为不可用。**幂等**（07 §1.3 第 2 条）：
    // 已经是不可用了就不再改状态、不再重复计数、不再重复记日志。
    void markChannelUnavailable(bbcoop::ipc::ChannelId channel,
                                UnavailableReason reason) noexcept;

    // 向 IPC 层注册「通道关闭」回调（同一条通道只注册一次）。
    // 死亡由操作系统通知，Kernel **不轮询**进程句柄（07-kernel-seam §1.2①）。
    void registerChannelWatch(bbcoop::ipc::ChannelId channel, Slot& slot) noexcept;

    // IPC 回调的跳板：函数指针 + void* 是 07 §1.2 定下的形状。
    static void onChannelClosedThunk(void* user, bbcoop::ipc::ChannelId channel) noexcept;

    KernelPolicy m_policy;
    KernelLogger m_logger;
    EventBus m_events;
    ServiceTable m_services;
    bbcoop::ipc::IAuxLiveness* m_liveness = nullptr;
    Slot m_slots[kMaxModules];
    std::size_t m_moduleCount = 0;

    // 自检报告用的连续快照：槽位里的 status 不连续（Slot 还有别的成员），
    // 所以报告不能直接拿 &m_slots[0].status 当数组。
    ModuleStatus m_reportBuffer[kMaxModules];

    // 没加载的模块及其原因，来自模块表。指针指向调用方（生成物或测试）持有的数组，
    // 生命周期必须覆盖 Kernel 的使用期 —— 生成物是静态存储，测试里是栈上的常量数组。
    const char* const* m_skippedIds = nullptr;
    const char* const* m_skippedReasons = nullptr;
    std::size_t m_skippedCount = 0;

    std::uint64_t m_lastSweepMs = 0;
    bool m_sweepDone = false;
};

}  // namespace bbcoop::kernel
