// bloodcoop-host：模块宿主的可执行入口（T-005 产出）。
//
// 命令行：
//   --self-test        加载模块、初始化、汇总健康，把 JSON 打到 stdout，然后退出。
//                      不带参数时就是这个行为。
//   --help             用法
//   --version          版本
//
// 退出码沿用仓库既有约定（tools/bbcoop.py / tools/preci/README.md：
// 0 通过 / 1 有失败 / 2 有未验证项（≠通过）/ 3 无法启动）：
//   0  overall = pass
//   1  overall = fail（required 失败）—— 任务书 §41.1 T-014 的验收
//   2  overall = degraded：宿主活着，但能力不全（例如一个模块都没加载）
//   3  用法错误 / 无法启动
//
// 「degraded 返回 2 而不是 0」是有意的：它表示**没有失败，但不等于通过**。
// 把它算成 0 会让 T-014 的 Gate 6（要求 overall 为 pass）在框架期就悄悄成立。
//
// stdout 只放 JSON，日志一律走 stderr（见 KernelLogger 的说明），
// 所以 `bloodcoop-host --self-test | python -c "import json,sys;json.load(sys.stdin)"`
// 不需要重定向 stderr。
//
// ── 模块表为空（现在就是这样，不是 bug）──────────────────────────────────────
//
// 模块的实现属 T-007，配置文件属 T-008，两个可执行目标与 placement 分派属 T-009。
// 在那之前这里给 Kernel 一张**空表**：Kernel 库本身不依赖任何模块
// （删掉任意模块目录都还能构建），自检输出 modules 为空、overall = degraded、退出码 2。
// 这是诚实的状态，不是「假装通过」。

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/module_table.h"
#include "bbcoop/kernel/self_test.h"

#ifndef BBCOOP_VERSION_STRING
#define BBCOOP_VERSION_STRING "0.0.0-unknown"
#endif

namespace {

// 宿主当前使用的模块表。
//
// 为什么是空表：见文件头。T-008 产出 `config/modules.yaml`、T-009 按 `placement`
// 把模块分派到两个目标之后，这里改成读生成物即可 —— Kernel 侧不用动。
bbcoop::kernel::ModuleTable makeModuleTable() noexcept {
    bbcoop::kernel::ModuleTable table;
    table.loadable = nullptr;
    table.requiredFlags = nullptr;
    table.loadableCount = 0;
    table.remote = nullptr;
    table.remoteCount = 0;
    table.skippedIds = nullptr;
    table.skippedReasons = nullptr;
    table.skippedCount = 0;
    return table;
}

// 违反 noexcept 契约的模块会让 std::terminate 被调用（见 kernel.h 的说明）。
// 这里至少把「为什么进程突然没了」写清楚 —— 但**不能**让进程继续跑：
// terminate 之后没有可恢复的现场，进程内隔离到此为止。
void onTerminate() noexcept {
    std::fputs("[FATAL] 有代码违反了 noexcept 契约（模块边界上抛出了异常），进程终止；"
               "进程内无法恢复，见 docs/architecture/06-process-boundary.md §6\n",
               stderr);
    std::fflush(stderr);
    std::abort();
}

// 用法错误 / 无法启动。
constexpr int kExitUsage = 3;

void printUsage() {
    std::fputs("用法：bloodcoop-host [--self-test] [--help] [--version]\n"
               "  --self-test  加载模块并输出自检 JSON（默认行为）\n"
               "  退出码：0 = pass，1 = required 失败，2 = degraded（不等于通过），3 = 用法错误\n",
               stderr);
}

int runSelfTest() {
    bbcoop::kernel::Kernel kernel;
    const bbcoop::kernel::ModuleTable table = makeModuleTable();

    kernel.loadTable(table);
    const bbcoop::kernel::SelfTestReport report = kernel.selfTestReport();
    const std::string json = bbcoop::kernel::renderSelfTestJson(report, BBCOOP_VERSION_STRING);
    std::fwrite(json.data(), 1, json.size(), stdout);
    std::fflush(stdout);

    kernel.shutdownAll();

    switch (report.overall) {
        case bbcoop::kernel::SelfTestOverall::Pass:
            return 0;
        case bbcoop::kernel::SelfTestOverall::Fail:
            return 1;  // required 失败（任务书 §41.1 T-014）
        case bbcoop::kernel::SelfTestOverall::Degraded:
            return 2;  // 没有失败，但不等于通过
    }
    return kExitUsage;
}

}  // namespace

int main(int argc, char** argv) {
    std::set_terminate(&onTerminate);

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage();
            return 0;
        }
        if (std::strcmp(arg, "--version") == 0) {
            std::printf("%s\n", BBCOOP_VERSION_STRING);
            return 0;
        }
        if (std::strcmp(arg, "--self-test") == 0) {
            continue;
        }
        std::fprintf(stderr, "未知参数：%s\n", arg);
        printUsage();
        return kExitUsage;
    }

    return runSelfTest();
}
