#pragma once

#include <cstddef>
#include <cstdint>

namespace plib {

using byte = unsigned char;
using u8 = std::uint8_t;
using i8 = std::int8_t;
using u16 = std::uint16_t;
using i16 = std::int16_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;
using usize = std::size_t;
using f32 = float;
using f64 = double;

static_assert(sizeof(byte) == 1, "Size of byte is not correct");
static_assert(sizeof(f32) == 4, "Size of float is not 4 bytes");
static_assert(sizeof(f64) == 8, "Size of double is not 8 bytes");

template<typename T>
using owner = T;

}