#pragma once

#include <cstdint>

namespace nytl {}

namespace imgio {

using namespace nytl;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

class Read;
class Write;
class ImageProvider;
class FileHandle;
struct ImageData;

} // namespace imgio
