// KernelLogger 与 ServiceTable / ModuleServiceView 的实现（T-005）。
//
// 这两个类实现的是 src/interfaces/module_context.h 里的冻结接口。
// 它们的共同约束是 noexcept：所有可能失败的操作都必须返回错误码，
// 不能把异常漏出去（任务书 §3.6 原则 5）。

#include "bbcoop/kernel/host_services.h"

#include <cstdio>
#include <cstring>

namespace bbcoop::kernel {

namespace {

constexpr std::size_t kLogLineCapacity = 512;

const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

bool sameKey(const char* left, int leftMajor, const char* right, int rightMajor) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return leftMajor == rightMajor && std::strcmp(left, right) == 0;
}

}  // namespace

// ── KernelLogger ────────────────────────────────────────────────────────────

void KernelLogger::log(LogLevel level, const char* module_id, const char* message) noexcept {
    if (static_cast<std::int32_t>(level) < static_cast<std::int32_t>(m_minLevel)) {
        ++m_dropped;
        return;
    }

    char line[kLogLineCapacity];
    const int written = std::snprintf(line, sizeof(line), "[%s] [%s] %s\n",
                                      levelName(level),
                                      module_id != nullptr ? module_id : "-",
                                      message != nullptr ? message : "-");
    if (written <= 0) {
        return;
    }
    // snprintf 截断时返回值大于缓冲区，这里只需要保证字符串以 0 结尾。
    line[kLogLineCapacity - 1] = '\0';

    ++m_written;
    if (m_sink != nullptr) {
        m_sink(m_user, line);
        return;
    }
    // 默认写 stderr：stdout 留给 --self-test 的 JSON，两者不能混。
    std::fputs(line, stderr);
}

void KernelLogger::setMinLevel(LogLevel level) noexcept {
    m_minLevel = level;
}

LogLevel KernelLogger::minLevel() const noexcept {
    return m_minLevel;
}

void KernelLogger::setSink(Sink sink, void* user) noexcept {
    m_sink = sink;
    m_user = user;
}

std::uint32_t KernelLogger::droppedCount() const noexcept {
    return m_dropped;
}

std::uint32_t KernelLogger::writtenCount() const noexcept {
    return m_written;
}

// ── ServiceTable ────────────────────────────────────────────────────────────

Error ServiceTable::publishFor(const char* ownerId, const char* name, int major, void* service) noexcept {
    if (name == nullptr || name[0] == '\0' || service == nullptr) {
        return Error::InvalidArgument;
    }

    for (std::size_t i = 0; i < kMaxServiceEntries; ++i) {
        const Entry& entry = m_entries[i];
        if (!entry.used) {
            continue;
        }
        if (sameKey(entry.name, entry.major, name, major)) {
            // 冻结接口的约定：重复发布返回 Refused，且**不覆盖**已有实现。
            // 覆盖会让「谁在提供服务」变得不可预测。
            if (m_logger != nullptr) {
                m_logger->log(LogLevel::Warn, ownerId != nullptr ? ownerId : "kernel",
                              "publish refused: service already published");
            }
            return Error::Refused;
        }
    }

    for (std::size_t i = 0; i < kMaxServiceEntries; ++i) {
        Entry& entry = m_entries[i];
        if (entry.used) {
            continue;
        }
        entry.name = name;
        entry.major = major;
        entry.service = service;
        entry.owner = ownerId;
        entry.used = true;
        ++m_size;
        return Error::Ok;
    }

    if (m_logger != nullptr) {
        m_logger->log(LogLevel::Warn, ownerId != nullptr ? ownerId : "kernel",
                      "publish refused: service table is full");
    }
    return Error::Refused;
}

Error ServiceTable::withdrawFor(const char* ownerId, const char* name, int major) noexcept {
    if (name == nullptr || name[0] == '\0') {
        return Error::InvalidArgument;
    }

    for (std::size_t i = 0; i < kMaxServiceEntries; ++i) {
        Entry& entry = m_entries[i];
        if (!entry.used || !sameKey(entry.name, entry.major, name, major)) {
            continue;
        }
        // 模块只能撤销自己发布的服务：撤销别人的会让「谁的发布」失去意义。
        if (ownerId != nullptr && (entry.owner == nullptr || std::strcmp(entry.owner, ownerId) != 0)) {
            return Error::Refused;
        }
        entry = Entry{};
        --m_size;
        return Error::Ok;
    }
    return Error::Unavailable;
}

std::size_t ServiceTable::withdrawAllFor(const char* ownerId) noexcept {
    if (ownerId == nullptr) {
        return 0;
    }
    std::size_t removed = 0;
    for (std::size_t i = 0; i < kMaxServiceEntries; ++i) {
        Entry& entry = m_entries[i];
        if (!entry.used || entry.owner == nullptr) {
            continue;
        }
        if (std::strcmp(entry.owner, ownerId) == 0) {
            entry = Entry{};
            --m_size;
            ++removed;
        }
    }
    return removed;
}

void* ServiceTable::find(const char* name, int major) const noexcept {
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < kMaxServiceEntries; ++i) {
        const Entry& entry = m_entries[i];
        if (entry.used && sameKey(entry.name, entry.major, name, major)) {
            return entry.service;
        }
    }
    // 返回 nullptr 表示服务当前不可用，调用方必须处理它（冻结接口的约定）。
    return nullptr;
}

std::size_t ServiceTable::size() const noexcept {
    return m_size;
}

void ServiceTable::setLogger(ILogger* logger) noexcept {
    m_logger = logger;
}

// ── ModuleServiceView ───────────────────────────────────────────────────────

void ModuleServiceView::bind(ServiceTable& table, const char* ownerId) noexcept {
    m_table = &table;
    m_ownerId = ownerId;
}

void* ModuleServiceView::find(const char* service_name, int version_major) noexcept {
    if (m_table == nullptr) {
        return nullptr;
    }
    return m_table->find(service_name, version_major);
}

Error ModuleServiceView::publish(const char* service_name, int version_major, void* service) noexcept {
    if (m_table == nullptr) {
        return Error::Refused;
    }
    return m_table->publishFor(m_ownerId, service_name, version_major, service);
}

Error ModuleServiceView::withdraw(const char* service_name, int version_major) noexcept {
    if (m_table == nullptr) {
        return Error::Refused;
    }
    return m_table->withdrawFor(m_ownerId, service_name, version_major);
}

const char* ModuleServiceView::ownerId() const noexcept {
    return m_ownerId;
}

}  // namespace bbcoop::kernel
