// `--self-test` 的 JSON 渲染（T-005）。
//
// 手写而不是引 JSON 库：C++ 标准库没有 JSON，而第三方依赖白名单任务书没有给出
// （docs/architecture/02-module-contract.md M-6）。输出是固定结构的一小段文本，
// 手写的成本低于引入与审计一个依赖。
//
// 两条容易出错的地方，这里显式处理：
//   * 字符串转义：模块 ID、health、last_error 目前都来自编译期常量（ASCII），
//     但仍然做转义 —— 生成物改坏时宁可输出 \u00XX 也不要吐出非法 JSON。
//   * 数字格式：interface_version 与计数用无符号整型输出，避免本地化影响。

#include "bbcoop/kernel/self_test.h"

#include <cstdio>
#include <string>

namespace bbcoop::kernel {

namespace {

// 转义 JSON 字符串。控制字符写成 \u00XX，其余按原样。
std::string escape(const char* text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const unsigned char ch = static_cast<unsigned char>(*cursor);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20U) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
                    out += buffer;
                } else {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    return out;
}

const char* orUnknown(const char* text) noexcept {
    return text != nullptr ? text : "unknown";
}

void appendModule(std::string& out, const ModuleStatus& status, bool last) {
    out += "    {\n";
    out += "      \"id\": \"";
    out += escape(status.id);
    out += "\",\n";
    out += "      \"source\": \"";
    out += to_string(status.source);
    out += "\",\n";
    out += "      \"health\": \"";
    out += to_string(status.health);
    out += "\",\n";
    out += std::string("      \"initialized\": ") + (status.initialized ? "true" : "false") + ",\n";
    out += std::string("      \"circuit_open\": ") + (status.circuitOpen ? "true" : "false") + ",\n";
    out += std::string("      \"required\": ") + (status.required ? "true" : "false") + ",\n";
    out += "      \"last_error\": \"";
    out += to_string(status.lastError);
    out += "\",\n";
    // `07-kernel-seam.md` §1.5 要的是 null，而 ipc 的 to_string 给的是 "none"。
    // 直接把 "none" 写进 JSON 会让「没有失败」和「失败原因是 none」分不清，
    // 所以这里显式特判成 null —— 冻结枚举的 to_string 不动。
    out += "      \"unavailable_reason\": ";
    if (status.unavailableReason == UnavailableReason::None) {
        out += "null,\n";
    } else {
        out += "\"";
        out += bbcoop::ipc::to_string(status.unavailableReason);
        out += "\",\n";
    }
    out += "      \"tick_calls\": " + std::to_string(status.tickCalls) + ",\n";
    out += "      \"tick_timeouts\": " + std::to_string(status.tickTimeouts) + "\n";
    out += last ? "    }\n" : "    },\n";
}

void appendSkipped(std::string& out, const char* id, const char* reason, bool last) {
    out += "    {\"id\": \"";
    out += escape(id);
    out += "\", \"reason\": \"";
    out += escape(orUnknown(reason));
    out += last ? "\"}\n" : "\"},\n";
}

}  // namespace

std::string renderSelfTestJson(const SelfTestReport& report, const char* version) {
    std::string out;
    out += "{\n";
    out += "  \"interface_version\": " + std::to_string(kInterfaceVersion) + ",\n";
    out += "  \"overall\": \"";
    out += to_string(report.overall);
    out += "\",\n";

    out += "  \"modules\": [\n";
    if (report.modules != nullptr) {
        for (std::size_t i = 0; i < report.moduleCount; ++i) {
            appendModule(out, report.modules[i], i + 1 == report.moduleCount);
        }
    }
    out += "  ],\n";

    out += "  \"skipped\": [\n";
    for (std::size_t i = 0; i < report.skippedCount; ++i) {
        const char* id = report.skippedIds != nullptr ? report.skippedIds[i] : nullptr;
        const char* reason = report.skippedReasons != nullptr ? report.skippedReasons[i] : nullptr;
        appendSkipped(out, id, reason, i + 1 == report.skippedCount);
    }
    out += "  ],\n";

    out += "  \"version\": \"";
    out += escape(orUnknown(version));
    out += "\"\n";
    out += "}\n";
    return out;
}

}  // namespace bbcoop::kernel
