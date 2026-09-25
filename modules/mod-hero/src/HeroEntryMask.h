#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace evry::advancement {
// Stable catalog positions, serialized as exactly 32 lowercase hex digits.
// No floating-point or platform-width conversions carry ownership bits.
struct EntryMask {
    std::array<std::uint32_t, 4> words{};
    constexpr EntryMask(std::uint32_t low = 0) : words{low, 0, 0, 0} {}
    static constexpr EntryMask Bit(std::size_t index) {
        EntryMask result;
        if (index < 128) result.words[index / 32] = std::uint32_t{1} << (index % 32);
        return result;
    }
    static constexpr EntryMask First(unsigned count) {
        EntryMask result;
        for (unsigned i = 0; i < count && i < 128; ++i) result |= Bit(i);
        return result;
    }
    constexpr bool has(std::size_t index) const { return bool(*this & Bit(index)); }
    constexpr explicit operator bool() const { return words[0] || words[1] || words[2] || words[3]; }
    constexpr bool operator==(EntryMask const&) const = default;
    constexpr EntryMask& operator|=(EntryMask b) {
        for (unsigned i = 0; i < 4; ++i) words[i] |= b.words[i];
        return *this;
    }
    constexpr EntryMask& operator&=(EntryMask b) {
        for (unsigned i = 0; i < 4; ++i) words[i] &= b.words[i];
        return *this;
    }
    friend constexpr EntryMask operator|(EntryMask a, EntryMask b) { return a |= b; }
    friend constexpr EntryMask operator&(EntryMask a, EntryMask b) { return a &= b; }
    friend constexpr EntryMask operator~(EntryMask a) {
        for (auto& word : a.words) word = ~word;
        return a;
    }
    void write(char (&output)[33]) const noexcept {
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < 32; ++i) output[31 - i] = digits[(words[i / 8] >> ((i % 8) * 4)) & 15];
        output[32] = 0;
    }
    static constexpr bool Parse(std::string_view text, EntryMask& output) {
        if (text.size() != 32) return false;
        EntryMask candidate;
        for (unsigned i = 0; i < 32; ++i) {
            char c = text[31 - i];
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
            if (digit == 16) return false;
            candidate.words[i / 8] |= digit << ((i % 8) * 4);
        }
        output = candidate;
        return true;
    }
};
}
