#pragma once

// Reading and writing guest (N64) memory from BlockPlanet code. RDRAM is held
// as host-order 32-bit words: bytes live at address ^ 3 and halfwords at
// address ^ 2. Addresses are N64 virtual addresses (0x80000000 and up).

#include <cstdint>
#include <cstring>

namespace tetrisphere::guest {

inline std::uint8_t* host(std::uint8_t* rdram, std::uint32_t address) { return rdram + (address & 0x00FFFFFFu); }
inline const std::uint8_t* host(const std::uint8_t* rdram, std::uint32_t address) {
    return rdram + (address & 0x00FFFFFFu);
}
inline std::uint8_t load8(const std::uint8_t* rdram, std::uint32_t address) { return *host(rdram, address ^ 3u); }
inline std::int16_t load16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t v;
    std::memcpy(&v, host(rdram, address ^ 2u), 2);
    return v;
}
inline std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, host(rdram, address), 4);
    return v;
}
inline void store8(std::uint8_t* rdram, std::uint32_t address, std::uint8_t v) { *host(rdram, address ^ 3u) = v; }
inline void store16(std::uint8_t* rdram, std::uint32_t address, std::int16_t v) {
    std::memcpy(host(rdram, address ^ 2u), &v, 2);
}
inline void store32(std::uint8_t* rdram, std::uint32_t address, std::uint32_t v) {
    std::memcpy(host(rdram, address), &v, 4);
}

} // namespace tetrisphere::guest
