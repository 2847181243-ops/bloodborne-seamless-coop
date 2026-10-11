// T-021：可跨边界类型的最小线格式（小端、定长）。
// owner: AI-00（T-021）。
//
// 只支持 08-ipc.md D-5 白名单里的类型：bool / u32 / u64 / 字节块。
// 指针、引用、STL 容器一律不在这里出现。

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace bbcoop::ipc::wire {

inline constexpr std::size_t kU32Size = 4;
inline constexpr std::size_t kU64Size = 8;

inline void writeU32(std::span<std::byte> out, std::uint32_t v) noexcept {
    for (std::size_t i = 0; i < kU32Size && i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((v >> (8U * i)) & 0xFFU);
    }
}

inline std::uint32_t readU32(std::span<const std::byte> in) noexcept {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < kU32Size && i < in.size(); ++i) {
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[i])) << (8U * i);
    }
    return v;
}

inline void writeU64(std::span<std::byte> out, std::uint64_t v) noexcept {
    for (std::size_t i = 0; i < kU64Size && i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((v >> (8U * i)) & 0xFFU);
    }
}

inline std::uint64_t readU64(std::span<const std::byte> in) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < kU64Size && i < in.size(); ++i) {
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[i])) << (8U * i);
    }
    return v;
}

inline void writeBool(std::span<std::byte> out, bool v) noexcept {
    if (!out.empty()) {
        out[0] = static_cast<std::byte>(v ? 1U : 0U);
    }
}

inline bool readBool(std::span<const std::byte> in) noexcept {
    return !in.empty() && static_cast<unsigned char>(in[0]) != 0U;
}

}  // namespace bbcoop::ipc::wire
