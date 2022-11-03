#include <imgio/format.hpp>
#include <imgio/f16.hpp>
#include <imgio/allocation.hpp>
#include <nytl/vecOps.hpp>
#include <nytl/bytes.hpp>
#include <dlg/dlg.hpp>
#include <cmath>
#include "../format_utils.h"

namespace imgio {

using nytl::read;
using nytl::write;

u32 formatElementSize(Format format, FormatAspect aspect) {
	return FormatElementSize(VkFormat(format),
		VkImageAspectFlagBits(aspect));
}

u32 formatElementSize(Format format) {
	u32 ret = FormatElementSize(VkFormat(format), VK_IMAGE_ASPECT_COLOR_BIT);
	if(FormatHasDepth(VkFormat(format))) {
		ret += FormatDepthSize(VkFormat(format));
	}
	if(FormatHasStencil(VkFormat(format))) {
		ret += FormatStencilSize(VkFormat(format));
	}
	return ret;
}

Vec3ui blockSize(Format format) {
	auto [w, h, d] = FormatTexelBlockExtent(VkFormat(format));
	return {w, h, d};
}

bool isSRGB(Format format) {
	return FormatIsSRGB(VkFormat(format));
}

Format toggleSRGB(Format format) {
	switch(format) {
		case Format::r8Srgb:
			return Format::r8Unorm;
		case Format::r8g8Srgb:
			return Format::r8g8Unorm;
		case Format::r8g8b8Srgb:
			return Format::r8g8b8Unorm;
		case Format::r8g8b8a8Srgb:
			return Format::r8g8b8a8Unorm;
		case Format::b8g8r8a8Srgb:
			return Format::b8g8r8a8Unorm;
		case Format::b8g8r8Srgb:
			return Format::b8g8r8Unorm;
		case Format::a8b8g8r8SrgbPack32:
			return Format::a8b8g8r8UnormPack32;

		case Format::r8Unorm:
			return Format::r8Srgb;
		case Format::r8g8Unorm:
			return Format::r8g8Srgb;
		case Format::r8g8b8Unorm:
			return Format::r8g8b8Srgb;
		case Format::r8g8b8a8Unorm:
			return Format::r8g8b8a8Srgb;
		case Format::b8g8r8a8Unorm:
			return Format::b8g8r8a8Srgb;
		case Format::b8g8r8Unorm:
			return Format::b8g8r8Srgb;
		case Format::a8b8g8r8UnormPack32:
			return Format::a8b8g8r8SrgbPack32;

		case Format::bc7UnormBlock:
			return Format::bc7SrgbBlock;
		case Format::bc7SrgbBlock:
			return Format::bc7UnormBlock;

		default: return format;
	}
}

// - https://en.wikipedia.org/wiki/SRGB (conversion matrices from here)
// - https://www.w3.org/Graphics/Color/srgb
double linearToSRGB(double linear) {
	if(linear < 0.0031308) {
		return 12.92 * linear;
	} else {
		return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
	}
}

double srgbToLinear(double srgb) {
	if(srgb < 0.04045) {
		return srgb / 12.92;
	} else {
		return std::pow((srgb + 0.055) / 1.055, 2.4);
	}
}

Vec4d linearToSRGB(Vec4d v) {
	v[0] = linearToSRGB(v[0]);
	v[1] = linearToSRGB(v[1]);
	v[2] = linearToSRGB(v[2]);
	return v;
}

Vec4d srgbToLinear(Vec4d v) {
	v[0] = srgbToLinear(v[0]);
	v[1] = srgbToLinear(v[1]);
	v[2] = srgbToLinear(v[2]);
	return v;
}

// reads formats
struct FormatReader {
	template<std::size_t N, typename T, u32 Fac, bool SRGB>
	static void call(span<const std::byte>& src, Vec4d& dst) {
		using nytl::vec::operators::operator/;
		auto ret = read<Vec<N, T>>(src);
		// TODO: strictly speaking we have to clamp for normed formats
		// (so that we can't ever get 1.0..01 for SNORM formats)
		dst = Vec4d(ret) / double(Fac);
		if constexpr(SRGB) {
			dst = srgbToLinear(dst);
		}
	}

	// packed formats
	template<bool Norm, bool Signed>
	static void unpack(u32& src, Vec4d& dst, u32) {
		// nothing to do, no bits remaining
		(void) src;
		(void) dst;
	}

	template<bool Norm, bool Signed, u32 FirstBits, u32... Rest>
	static void unpack(u32& src, Vec4d& dst, u32 id) {
		unpack<Norm, Signed, Rest...>(src, dst, id + 1);

		auto limit = 1u << FirstBits;
		auto mask = limit - 1; // first (FirstBits-1) bits set to 1

		auto valUint = (src & mask);
		double converted = valUint;

		if constexpr(Signed) {
			auto halfLimit = limit / 2;
			converted = converted - double(halfLimit);
			mask = halfLimit - 1; // if it's normalized
		}

		if constexpr(Norm) {
			converted = converted / double(mask);
		}

		dst[id] = converted;
		src = src >> FirstBits;
	}

	template<bool Norm, bool Signed, bool SRGB, u32... Bits>
	static void callPack(span<const std::byte>& src, Vec4d& dst) {
		u32 packed;
		constexpr u32 numBits = (0 + ... + Bits);
		if constexpr(numBits == 8) {
			packed = read<u8>(src);
		} else if(numBits == 16) {
			packed = read<u16>(src);
		} else {
			static_assert(numBits, "only 8/16/32 bit packed formats supported");
			packed = read<u32>(src);
		}

		unpack<Norm, Signed, Bits...>(packed, dst, 0u);

		// TODO: strictly speaking we have to clamp for normed formats
		// (so that we can't ever get 1.0..01 for SNORM formats)
		using nytl::vec::operators::operator/;

		if constexpr(SRGB) {
			static_assert(!Signed && Norm);
			dst = srgbToLinear(dst);
		}
	}
};

// writes formats
struct FormatWriter {
	template<std::size_t N, typename T, u32 Fac, bool SRGB>
	static void call(span<std::byte>& dst, Vec4d src) {
		if constexpr(SRGB) {
			src = linearToSRGB(src);
		}

		for(auto i = 0u; i < N; ++i) {
			write<T>(dst, T(Fac * src[i]));
		}
	}

	// packed formats
	template<bool Norm, bool Signed>
	static void pack(u32& dst, const Vec4d& src, u32) {
		// nothing to do, no bits remaining
		(void) src;
		(void) dst;
	}

	template<bool Norm, bool Signed, u32 FirstBits, u32... Rest>
	static void pack(u32& dst, const Vec4d& src, u32 id) {
		auto converted = src[id];

		auto limit = 1u << FirstBits;
		auto mask = limit - 1; // first (FirstBits-1) bits set to 1
		float signFac = 1.0;

		if constexpr(Norm) {
			converted *= double(mask);
			signFac = 0.5; // if it's also signed
		}

		if constexpr(Signed) {
			auto halfLimit = double(limit >> 1u);
			converted = signFac * converted + halfLimit;
		}

		u32 valUint = u32(converted);

		dst = (dst << FirstBits) | (valUint & mask);
		pack<Norm, Signed, Rest...>(dst, src, id + 1);
	}

	template<bool Norm, bool Signed, bool SRGB, u32... Bits>
	static void callPack(span<std::byte>& dst, Vec4d src) {
		if constexpr(SRGB) {
			static_assert(!Signed && Norm);
			src = linearToSRGB(src);
		}

		u32 packed {};
		pack<Norm, Signed, Bits...>(packed, src, 0u);

		constexpr u32 numBits = (0 + ... + Bits);
		if constexpr(numBits == 8) {
			write<u8>(dst, packed);
		} else if(numBits == 16) {
			write<u16>(dst, packed);
		} else {
			static_assert(numBits, "only 8/16/32 bit packed formats supported");
			write<u32>(dst, packed);
		}
	}
};

template<bool Write, std::size_t N, typename T, u32 Fac = 1, bool SRGB = false,
	typename Span, typename Vec>
void iofmt(Span& span, Vec& vec) {
	if constexpr(Write) {
		FormatWriter::call<N, T, Fac, SRGB>(span, vec);
	} else {
		FormatReader::call<N, T, Fac, SRGB>(span, vec);
	}
}

// like iofmt but for packed formats
template<bool Write, bool Norm, bool Signed, bool SRGB, u32... Bits,
	typename Span, typename Vec>
void iopack(Span& span, Vec& vec) {
	if constexpr(Write) {
		FormatWriter::callPack<Norm, Signed, SRGB, Bits...>(span, vec);
	} else {
		FormatReader::callPack<Norm, Signed, SRGB, Bits...>(span, vec);
	}
}

template<bool Write, FORMAT_NUMERICAL_TYPE type, u32... Bits,
	typename Span, typename Vec>
void iopack(Span& span, Vec& vec) {
	using FMT = FORMAT_NUMERICAL_TYPE;
	iopack<Write,
		type == FMT::UNORM || type == FMT::SRGB || type == FMT::SNORM,
		type == FMT::SNORM || type == FMT::SINT || type == FMT::SSCALED,
		type == FMT::SRGB,
		Bits...>(span, vec);
}

// swizzle
template<bool Reverse, unsigned A, unsigned B = 1u, unsigned C = 2u, unsigned D = 3u>
Vec4d swizzle(Vec4d x) {
	static_assert(A <= 3 && B <= 3 && C <= 3 && D <= 3);

	if constexpr(Reverse) {
		Vec4d ret;
		ret[A] = x[0];
		ret[B] = x[1];
		ret[C] = x[2];
		ret[D] = x[3];
		return ret;
	} else {
		return Vec4d{x[A], x[B], x[C], x[D]};
	}
}

inline const char* name(Format val) {
	switch(val) {
		case Format::undefined: return "undefined";
		case Format::r4g4UnormPack8: return "r4g4UnormPack8";
		case Format::r4g4b4a4UnormPack16: return "r4g4b4a4UnormPack16";
		case Format::b4g4r4a4UnormPack16: return "b4g4r4a4UnormPack16";
		case Format::r5g6b5UnormPack16: return "r5g6b5UnormPack16";
		case Format::b5g6r5UnormPack16: return "b5g6r5UnormPack16";
		case Format::r5g5b5a1UnormPack16: return "r5g5b5a1UnormPack16";
		case Format::b5g5r5a1UnormPack16: return "b5g5r5a1UnormPack16";
		case Format::a1r5g5b5UnormPack16: return "a1r5g5b5UnormPack16";
		case Format::r8Unorm: return "r8Unorm";
		case Format::r8Snorm: return "r8Snorm";
		case Format::r8Uscaled: return "r8Uscaled";
		case Format::r8Sscaled: return "r8Sscaled";
		case Format::r8Uint: return "r8Uint";
		case Format::r8Sint: return "r8Sint";
		case Format::r8Srgb: return "r8Srgb";
		case Format::r8g8Unorm: return "r8g8Unorm";
		case Format::r8g8Snorm: return "r8g8Snorm";
		case Format::r8g8Uscaled: return "r8g8Uscaled";
		case Format::r8g8Sscaled: return "r8g8Sscaled";
		case Format::r8g8Uint: return "r8g8Uint";
		case Format::r8g8Sint: return "r8g8Sint";
		case Format::r8g8Srgb: return "r8g8Srgb";
		case Format::r8g8b8Unorm: return "r8g8b8Unorm";
		case Format::r8g8b8Snorm: return "r8g8b8Snorm";
		case Format::r8g8b8Uscaled: return "r8g8b8Uscaled";
		case Format::r8g8b8Sscaled: return "r8g8b8Sscaled";
		case Format::r8g8b8Uint: return "r8g8b8Uint";
		case Format::r8g8b8Sint: return "r8g8b8Sint";
		case Format::r8g8b8Srgb: return "r8g8b8Srgb";
		case Format::b8g8r8Unorm: return "b8g8r8Unorm";
		case Format::b8g8r8Snorm: return "b8g8r8Snorm";
		case Format::b8g8r8Uscaled: return "b8g8r8Uscaled";
		case Format::b8g8r8Sscaled: return "b8g8r8Sscaled";
		case Format::b8g8r8Uint: return "b8g8r8Uint";
		case Format::b8g8r8Sint: return "b8g8r8Sint";
		case Format::b8g8r8Srgb: return "b8g8r8Srgb";
		case Format::r8g8b8a8Unorm: return "r8g8b8a8Unorm";
		case Format::r8g8b8a8Snorm: return "r8g8b8a8Snorm";
		case Format::r8g8b8a8Uscaled: return "r8g8b8a8Uscaled";
		case Format::r8g8b8a8Sscaled: return "r8g8b8a8Sscaled";
		case Format::r8g8b8a8Uint: return "r8g8b8a8Uint";
		case Format::r8g8b8a8Sint: return "r8g8b8a8Sint";
		case Format::r8g8b8a8Srgb: return "r8g8b8a8Srgb";
		case Format::b8g8r8a8Unorm: return "b8g8r8a8Unorm";
		case Format::b8g8r8a8Snorm: return "b8g8r8a8Snorm";
		case Format::b8g8r8a8Uscaled: return "b8g8r8a8Uscaled";
		case Format::b8g8r8a8Sscaled: return "b8g8r8a8Sscaled";
		case Format::b8g8r8a8Uint: return "b8g8r8a8Uint";
		case Format::b8g8r8a8Sint: return "b8g8r8a8Sint";
		case Format::b8g8r8a8Srgb: return "b8g8r8a8Srgb";
		case Format::a8b8g8r8UnormPack32: return "a8b8g8r8UnormPack32";
		case Format::a8b8g8r8SnormPack32: return "a8b8g8r8SnormPack32";
		case Format::a8b8g8r8UscaledPack32: return "a8b8g8r8UscaledPack32";
		case Format::a8b8g8r8SscaledPack32: return "a8b8g8r8SscaledPack32";
		case Format::a8b8g8r8UintPack32: return "a8b8g8r8UintPack32";
		case Format::a8b8g8r8SintPack32: return "a8b8g8r8SintPack32";
		case Format::a8b8g8r8SrgbPack32: return "a8b8g8r8SrgbPack32";
		case Format::a2r10g10b10UnormPack32: return "a2r10g10b10UnormPack32";
		case Format::a2r10g10b10SnormPack32: return "a2r10g10b10SnormPack32";
		case Format::a2r10g10b10UscaledPack32: return "a2r10g10b10UscaledPack32";
		case Format::a2r10g10b10SscaledPack32: return "a2r10g10b10SscaledPack32";
		case Format::a2r10g10b10UintPack32: return "a2r10g10b10UintPack32";
		case Format::a2r10g10b10SintPack32: return "a2r10g10b10SintPack32";
		case Format::a2b10g10r10UnormPack32: return "a2b10g10r10UnormPack32";
		case Format::a2b10g10r10SnormPack32: return "a2b10g10r10SnormPack32";
		case Format::a2b10g10r10UscaledPack32: return "a2b10g10r10UscaledPack32";
		case Format::a2b10g10r10SscaledPack32: return "a2b10g10r10SscaledPack32";
		case Format::a2b10g10r10UintPack32: return "a2b10g10r10UintPack32";
		case Format::a2b10g10r10SintPack32: return "a2b10g10r10SintPack32";
		case Format::r16Unorm: return "r16Unorm";
		case Format::r16Snorm: return "r16Snorm";
		case Format::r16Uscaled: return "r16Uscaled";
		case Format::r16Sscaled: return "r16Sscaled";
		case Format::r16Uint: return "r16Uint";
		case Format::r16Sint: return "r16Sint";
		case Format::r16Sfloat: return "r16Sfloat";
		case Format::r16g16Unorm: return "r16g16Unorm";
		case Format::r16g16Snorm: return "r16g16Snorm";
		case Format::r16g16Uscaled: return "r16g16Uscaled";
		case Format::r16g16Sscaled: return "r16g16Sscaled";
		case Format::r16g16Uint: return "r16g16Uint";
		case Format::r16g16Sint: return "r16g16Sint";
		case Format::r16g16Sfloat: return "r16g16Sfloat";
		case Format::r16g16b16Unorm: return "r16g16b16Unorm";
		case Format::r16g16b16Snorm: return "r16g16b16Snorm";
		case Format::r16g16b16Uscaled: return "r16g16b16Uscaled";
		case Format::r16g16b16Sscaled: return "r16g16b16Sscaled";
		case Format::r16g16b16Uint: return "r16g16b16Uint";
		case Format::r16g16b16Sint: return "r16g16b16Sint";
		case Format::r16g16b16Sfloat: return "r16g16b16Sfloat";
		case Format::r16g16b16a16Unorm: return "r16g16b16a16Unorm";
		case Format::r16g16b16a16Snorm: return "r16g16b16a16Snorm";
		case Format::r16g16b16a16Uscaled: return "r16g16b16a16Uscaled";
		case Format::r16g16b16a16Sscaled: return "r16g16b16a16Sscaled";
		case Format::r16g16b16a16Uint: return "r16g16b16a16Uint";
		case Format::r16g16b16a16Sint: return "r16g16b16a16Sint";
		case Format::r16g16b16a16Sfloat: return "r16g16b16a16Sfloat";
		case Format::r32Uint: return "r32Uint";
		case Format::r32Sint: return "r32Sint";
		case Format::r32Sfloat: return "r32Sfloat";
		case Format::r32g32Uint: return "r32g32Uint";
		case Format::r32g32Sint: return "r32g32Sint";
		case Format::r32g32Sfloat: return "r32g32Sfloat";
		case Format::r32g32b32Uint: return "r32g32b32Uint";
		case Format::r32g32b32Sint: return "r32g32b32Sint";
		case Format::r32g32b32Sfloat: return "r32g32b32Sfloat";
		case Format::r32g32b32a32Uint: return "r32g32b32a32Uint";
		case Format::r32g32b32a32Sint: return "r32g32b32a32Sint";
		case Format::r32g32b32a32Sfloat: return "r32g32b32a32Sfloat";
		case Format::r64Uint: return "r64Uint";
		case Format::r64Sint: return "r64Sint";
		case Format::r64Sfloat: return "r64Sfloat";
		case Format::r64g64Uint: return "r64g64Uint";
		case Format::r64g64Sint: return "r64g64Sint";
		case Format::r64g64Sfloat: return "r64g64Sfloat";
		case Format::r64g64b64Uint: return "r64g64b64Uint";
		case Format::r64g64b64Sint: return "r64g64b64Sint";
		case Format::r64g64b64Sfloat: return "r64g64b64Sfloat";
		case Format::r64g64b64a64Uint: return "r64g64b64a64Uint";
		case Format::r64g64b64a64Sint: return "r64g64b64a64Sint";
		case Format::r64g64b64a64Sfloat: return "r64g64b64a64Sfloat";
		case Format::b10g11r11UfloatPack32: return "b10g11r11UfloatPack32";
		case Format::e5b9g9r9UfloatPack32: return "e5b9g9r9UfloatPack32";
		case Format::d16Unorm: return "d16Unorm";
		case Format::x8D24UnormPack32: return "x8D24UnormPack32";
		case Format::d32Sfloat: return "d32Sfloat";
		case Format::s8Uint: return "s8Uint";
		case Format::d16UnormS8Uint: return "d16UnormS8Uint";
		case Format::d24UnormS8Uint: return "d24UnormS8Uint";
		case Format::d32SfloatS8Uint: return "d32SfloatS8Uint";
		case Format::bc1RgbUnormBlock: return "bc1RgbUnormBlock";
		case Format::bc1RgbSrgbBlock: return "bc1RgbSrgbBlock";
		case Format::bc1RgbaUnormBlock: return "bc1RgbaUnormBlock";
		case Format::bc1RgbaSrgbBlock: return "bc1RgbaSrgbBlock";
		case Format::bc2UnormBlock: return "bc2UnormBlock";
		case Format::bc2SrgbBlock: return "bc2SrgbBlock";
		case Format::bc3UnormBlock: return "bc3UnormBlock";
		case Format::bc3SrgbBlock: return "bc3SrgbBlock";
		case Format::bc4UnormBlock: return "bc4UnormBlock";
		case Format::bc4SnormBlock: return "bc4SnormBlock";
		case Format::bc5UnormBlock: return "bc5UnormBlock";
		case Format::bc5SnormBlock: return "bc5SnormBlock";
		case Format::bc6hUfloatBlock: return "bc6hUfloatBlock";
		case Format::bc6hSfloatBlock: return "bc6hSfloatBlock";
		case Format::bc7UnormBlock: return "bc7UnormBlock";
		case Format::bc7SrgbBlock: return "bc7SrgbBlock";
		case Format::etc2R8g8b8UnormBlock: return "etc2R8g8b8UnormBlock";
		case Format::etc2R8g8b8SrgbBlock: return "etc2R8g8b8SrgbBlock";
		case Format::etc2R8g8b8a1UnormBlock: return "etc2R8g8b8a1UnormBlock";
		case Format::etc2R8g8b8a1SrgbBlock: return "etc2R8g8b8a1SrgbBlock";
		case Format::etc2R8g8b8a8UnormBlock: return "etc2R8g8b8a8UnormBlock";
		case Format::etc2R8g8b8a8SrgbBlock: return "etc2R8g8b8a8SrgbBlock";
		case Format::eacR11UnormBlock: return "eacR11UnormBlock";
		case Format::eacR11SnormBlock: return "eacR11SnormBlock";
		case Format::eacR11g11UnormBlock: return "eacR11g11UnormBlock";
		case Format::eacR11g11SnormBlock: return "eacR11g11SnormBlock";
		case Format::astc4x4UnormBlock: return "astc4x4UnormBlock";
		case Format::astc4x4SrgbBlock: return "astc4x4SrgbBlock";
		case Format::astc5x4UnormBlock: return "astc5x4UnormBlock";
		case Format::astc5x4SrgbBlock: return "astc5x4SrgbBlock";
		case Format::astc5x5UnormBlock: return "astc5x5UnormBlock";
		case Format::astc5x5SrgbBlock: return "astc5x5SrgbBlock";
		case Format::astc6x5UnormBlock: return "astc6x5UnormBlock";
		case Format::astc6x5SrgbBlock: return "astc6x5SrgbBlock";
		case Format::astc6x6UnormBlock: return "astc6x6UnormBlock";
		case Format::astc6x6SrgbBlock: return "astc6x6SrgbBlock";
		case Format::astc8x5UnormBlock: return "astc8x5UnormBlock";
		case Format::astc8x5SrgbBlock: return "astc8x5SrgbBlock";
		case Format::astc8x6UnormBlock: return "astc8x6UnormBlock";
		case Format::astc8x6SrgbBlock: return "astc8x6SrgbBlock";
		case Format::astc8x8UnormBlock: return "astc8x8UnormBlock";
		case Format::astc8x8SrgbBlock: return "astc8x8SrgbBlock";
		case Format::astc10x5UnormBlock: return "astc10x5UnormBlock";
		case Format::astc10x5SrgbBlock: return "astc10x5SrgbBlock";
		case Format::astc10x6UnormBlock: return "astc10x6UnormBlock";
		case Format::astc10x6SrgbBlock: return "astc10x6SrgbBlock";
		case Format::astc10x8UnormBlock: return "astc10x8UnormBlock";
		case Format::astc10x8SrgbBlock: return "astc10x8SrgbBlock";
		case Format::astc10x10UnormBlock: return "astc10x10UnormBlock";
		case Format::astc10x10SrgbBlock: return "astc10x10SrgbBlock";
		case Format::astc12x10UnormBlock: return "astc12x10UnormBlock";
		case Format::astc12x10SrgbBlock: return "astc12x10SrgbBlock";
		case Format::astc12x12UnormBlock: return "astc12x12UnormBlock";
		case Format::astc12x12SrgbBlock: return "astc12x12SrgbBlock";
		case Format::g8b8g8r8422Unorm: return "g8b8g8r8422Unorm";
		case Format::b8g8r8g8422Unorm: return "b8g8r8g8422Unorm";
		case Format::g8B8R83plane420Unorm: return "g8B8R83plane420Unorm";
		case Format::g8B8r82plane420Unorm: return "g8B8r82plane420Unorm";
		case Format::g8B8R83plane422Unorm: return "g8B8R83plane422Unorm";
		case Format::g8B8r82plane422Unorm: return "g8B8r82plane422Unorm";
		case Format::g8B8R83plane444Unorm: return "g8B8R83plane444Unorm";
		case Format::r10x6UnormPack16: return "r10x6UnormPack16";
		case Format::r10x6g10x6Unorm2pack16: return "r10x6g10x6Unorm2pack16";
		case Format::r10x6g10x6b10x6a10x6Unorm4pack16: return "r10x6g10x6b10x6a10x6Unorm4pack16";
		case Format::g10x6b10x6g10x6r10x6422Unorm4pack16: return "g10x6b10x6g10x6r10x6422Unorm4pack16";
		case Format::b10x6g10x6r10x6g10x6422Unorm4pack16: return "b10x6g10x6r10x6g10x6422Unorm4pack16";
		case Format::g10x6B10x6R10x63plane420Unorm3pack16: return "g10x6B10x6R10x63plane420Unorm3pack16";
		case Format::g10x6B10x6r10x62plane420Unorm3pack16: return "g10x6B10x6r10x62plane420Unorm3pack16";
		case Format::g10x6B10x6R10x63plane422Unorm3pack16: return "g10x6B10x6R10x63plane422Unorm3pack16";
		case Format::g10x6B10x6r10x62plane422Unorm3pack16: return "g10x6B10x6r10x62plane422Unorm3pack16";
		case Format::g10x6B10x6R10x63plane444Unorm3pack16: return "g10x6B10x6R10x63plane444Unorm3pack16";
		case Format::r12x4UnormPack16: return "r12x4UnormPack16";
		case Format::r12x4g12x4Unorm2pack16: return "r12x4g12x4Unorm2pack16";
		case Format::r12x4g12x4b12x4a12x4Unorm4pack16: return "r12x4g12x4b12x4a12x4Unorm4pack16";
		case Format::g12x4b12x4g12x4r12x4422Unorm4pack16: return "g12x4b12x4g12x4r12x4422Unorm4pack16";
		case Format::b12x4g12x4r12x4g12x4422Unorm4pack16: return "b12x4g12x4r12x4g12x4422Unorm4pack16";
		case Format::g12x4B12x4R12x43plane420Unorm3pack16: return "g12x4B12x4R12x43plane420Unorm3pack16";
		case Format::g12x4B12x4r12x42plane420Unorm3pack16: return "g12x4B12x4r12x42plane420Unorm3pack16";
		case Format::g12x4B12x4R12x43plane422Unorm3pack16: return "g12x4B12x4R12x43plane422Unorm3pack16";
		case Format::g12x4B12x4r12x42plane422Unorm3pack16: return "g12x4B12x4r12x42plane422Unorm3pack16";
		case Format::g12x4B12x4R12x43plane444Unorm3pack16: return "g12x4B12x4R12x43plane444Unorm3pack16";
		case Format::g16b16g16r16422Unorm: return "g16b16g16r16422Unorm";
		case Format::b16g16r16g16422Unorm: return "b16g16r16g16422Unorm";
		case Format::g16B16R163plane420Unorm: return "g16B16R163plane420Unorm";
		case Format::g16B16r162plane420Unorm: return "g16B16r162plane420Unorm";
		case Format::g16B16R163plane422Unorm: return "g16B16R163plane422Unorm";
		case Format::g16B16r162plane422Unorm: return "g16B16r162plane422Unorm";
		case Format::g16B16R163plane444Unorm: return "g16B16R163plane444Unorm";
		case Format::pvrtc12bppUnormBlockIMG: return "pvrtc12bppUnormBlockIMG";
		case Format::pvrtc14bppUnormBlockIMG: return "pvrtc14bppUnormBlockIMG";
		case Format::pvrtc22bppUnormBlockIMG: return "pvrtc22bppUnormBlockIMG";
		case Format::pvrtc24bppUnormBlockIMG: return "pvrtc24bppUnormBlockIMG";
		case Format::pvrtc12bppSrgbBlockIMG: return "pvrtc12bppSrgbBlockIMG";
		case Format::pvrtc14bppSrgbBlockIMG: return "pvrtc14bppSrgbBlockIMG";
		case Format::pvrtc22bppSrgbBlockIMG: return "pvrtc22bppSrgbBlockIMG";
		case Format::pvrtc24bppSrgbBlockIMG: return "pvrtc24bppSrgbBlockIMG";
		case Format::astc4x4SfloatBlockEXT: return "astc4x4SfloatBlockEXT";
		case Format::astc5x4SfloatBlockEXT: return "astc5x4SfloatBlockEXT";
		case Format::astc5x5SfloatBlockEXT: return "astc5x5SfloatBlockEXT";
		case Format::astc6x5SfloatBlockEXT: return "astc6x5SfloatBlockEXT";
		case Format::astc6x6SfloatBlockEXT: return "astc6x6SfloatBlockEXT";
		case Format::astc8x5SfloatBlockEXT: return "astc8x5SfloatBlockEXT";
		case Format::astc8x6SfloatBlockEXT: return "astc8x6SfloatBlockEXT";
		case Format::astc8x8SfloatBlockEXT: return "astc8x8SfloatBlockEXT";
		case Format::astc10x5SfloatBlockEXT: return "astc10x5SfloatBlockEXT";
		case Format::astc10x6SfloatBlockEXT: return "astc10x6SfloatBlockEXT";
		case Format::astc10x8SfloatBlockEXT: return "astc10x8SfloatBlockEXT";
		case Format::astc10x10SfloatBlockEXT: return "astc10x10SfloatBlockEXT";
		case Format::astc12x10SfloatBlockEXT: return "astc12x10SfloatBlockEXT";
		case Format::astc12x12SfloatBlockEXT: return "astc12x12SfloatBlockEXT";
		case Format::a4r4g4b4UnormPack16EXT: return "a4r4g4b4UnormPack16EXT";
		case Format::a4b4g4r4UnormPack16EXT: return "a4b4g4r4UnormPack16EXT";
		default: return nullptr;
	}
}

template<bool W, typename Span, typename Vec>
void ioFormat(Format format, Span& span, Vec& vec) {
	// TODO: missing:
	// - VK_FORMAT_B10G11R11_UFLOAT_PACK32
	// - (block-)compressed formats (can't be supported with this api anyways i guess)
	// 	- also multiplanar formats. But that's even harder, needs more complex api
	// 	  we'd only want cpu side decoding as a fallback anyways, we usually
	// 	  rely on being able to sample from more complicated formats
	// - Also properly test this!

	using FMT = FORMAT_NUMERICAL_TYPE;

	// We swizzle separately (in the calling function), so rgba here is the
	// same as bgra
	switch(VkFormat(format)) {
		case VK_FORMAT_R16_SFLOAT: return iofmt<W, 1, f16>(span, vec);
		case VK_FORMAT_R16G16_SFLOAT: return iofmt<W, 2, f16>(span, vec);
		case VK_FORMAT_R16G16B16_SFLOAT: return iofmt<W, 3, f16>(span, vec);
		case VK_FORMAT_R16G16B16A16_SFLOAT: return iofmt<W, 4, f16>(span, vec);

		case VK_FORMAT_R32_SFLOAT: return iofmt<W, 1, float>(span, vec);
		case VK_FORMAT_R32G32_SFLOAT: return iofmt<W, 2, float>(span, vec);
		case VK_FORMAT_R32G32B32_SFLOAT: return iofmt<W, 3, float>(span, vec);
		case VK_FORMAT_R32G32B32A32_SFLOAT: return iofmt<W, 4, float>(span, vec);

		case VK_FORMAT_R64_SFLOAT: return iofmt<W, 1, double>(span, vec);
		case VK_FORMAT_R64G64_SFLOAT: return iofmt<W, 2, double>(span, vec);
		case VK_FORMAT_R64G64B64_SFLOAT: return iofmt<W, 3, double>(span, vec);
		case VK_FORMAT_R64G64B64A64_SFLOAT: return iofmt<W, 4, double>(span, vec);

		case VK_FORMAT_R8_UNORM: return iofmt<W, 1, u8, 255>(span, vec);
		case VK_FORMAT_R8G8_UNORM: return iofmt<W, 2, u8, 255>(span, vec);
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_R8G8B8_UNORM:
			return iofmt<W, 3, u8, 255>(span, vec);
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_UNORM:
			return iofmt<W, 4, u8, 255>(span, vec);

		case VK_FORMAT_R8_SRGB: return iofmt<W, 1, u8, 255, true>(span, vec);
		case VK_FORMAT_R8G8_SRGB: return iofmt<W, 2, u8, 255, true>(span, vec);
		case VK_FORMAT_B8G8R8_SRGB:
		case VK_FORMAT_R8G8B8_SRGB:
			return iofmt<W, 3, u8, 255, true>(span, vec);
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_SRGB:
			return iofmt<W, 4, u8, 255, true>(span, vec);

		case VK_FORMAT_R16_UNORM: return iofmt<W, 1, u16, 65535>(span, vec);
		case VK_FORMAT_R16G16_UNORM: return iofmt<W, 2, u16, 65535>(span, vec);
		case VK_FORMAT_R16G16B16_UNORM: return iofmt<W, 3, u16, 65535>(span, vec);
		case VK_FORMAT_R16G16B16A16_UNORM: return iofmt<W, 4, u16, 65535>(span, vec);

		case VK_FORMAT_R8_SNORM: return iofmt<W, 1, i8, 127>(span, vec);
		case VK_FORMAT_R8G8_SNORM: return iofmt<W, 2, i8, 127>(span, vec);
		case VK_FORMAT_B8G8R8_SNORM:
		case VK_FORMAT_R8G8B8_SNORM:
			return iofmt<W, 3, i8, 127>(span, vec);
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
			return iofmt<W, 4, i8, 127>(span, vec);

		case VK_FORMAT_R16_SNORM: return iofmt<W, 1, i16, 32767>(span, vec);
		case VK_FORMAT_R16G16_SNORM: return iofmt<W, 2, i16, 32767>(span, vec);
		case VK_FORMAT_R16G16B16_SNORM: return iofmt<W, 3, i16, 32767>(span, vec);
		case VK_FORMAT_R16G16B16A16_SNORM: return iofmt<W, 4, i16, 32767>(span, vec);

		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_UINT: return iofmt<W, 1, u8>(span, vec);
		case VK_FORMAT_R8G8_USCALED:
		case VK_FORMAT_R8G8_UINT: return iofmt<W, 2, u8>(span, vec);
		case VK_FORMAT_R8G8B8_USCALED:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_B8G8R8_USCALED:
		case VK_FORMAT_B8G8R8_UINT:
			return iofmt<W, 3, u8>(span, vec);
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
			return iofmt<W, 4, u8>(span, vec);

		case VK_FORMAT_R16_USCALED:
		case VK_FORMAT_R16_UINT: return iofmt<W, 1, u16>(span, vec);
		case VK_FORMAT_R16G16_USCALED:
		case VK_FORMAT_R16G16_UINT: return iofmt<W, 2, u16>(span, vec);
		case VK_FORMAT_R16G16B16_USCALED:
		case VK_FORMAT_R16G16B16_UINT: return iofmt<W, 3, u16>(span, vec);
		case VK_FORMAT_R16G16B16A16_USCALED:
		case VK_FORMAT_R16G16B16A16_UINT: return iofmt<W, 4, u16>(span, vec);

		case VK_FORMAT_R32_UINT: return iofmt<W, 1, u32>(span, vec);
		case VK_FORMAT_R32G32_UINT: return iofmt<W, 2, u32>(span, vec);
		case VK_FORMAT_R32G32B32_UINT: return iofmt<W, 3, u32>(span, vec);
		case VK_FORMAT_R32G32B32A32_UINT: return iofmt<W, 4, u32>(span, vec);

		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_SINT: return iofmt<W, 1, i8>(span, vec);
		case VK_FORMAT_R8G8_SSCALED:
		case VK_FORMAT_R8G8_SINT: return iofmt<W, 2, i8>(span, vec);
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_B8G8R8_SSCALED:
		case VK_FORMAT_B8G8R8_SINT:
			return iofmt<W, 3, i8>(span, vec);
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_SINT:
			return iofmt<W, 4, i8>(span, vec);

		case VK_FORMAT_R16_SSCALED:
		case VK_FORMAT_R16_SINT: return iofmt<W, 1, i16>(span, vec);
		case VK_FORMAT_R16G16_SSCALED:
		case VK_FORMAT_R16G16_SINT: return iofmt<W, 2, i16>(span, vec);
		case VK_FORMAT_R16G16B16_SSCALED:
		case VK_FORMAT_R16G16B16_SINT: return iofmt<W, 3, i16>(span, vec);
		case VK_FORMAT_R16G16B16A16_SSCALED:
		case VK_FORMAT_R16G16B16A16_SINT: return iofmt<W, 4, i16>(span, vec);

		case VK_FORMAT_R32_SINT: return iofmt<W, 1, i32>(span, vec);
		case VK_FORMAT_R32G32_SINT: return iofmt<W, 2, i32>(span, vec);
		case VK_FORMAT_R32G32B32_SINT: return iofmt<W, 3, i32>(span, vec);
		case VK_FORMAT_R32G32B32A32_SINT: return iofmt<W, 4, i32>(span, vec);

		// NOTE: precision for 64-bit int formats can be problematic
		case VK_FORMAT_R64_UINT: return iofmt<W, 1, u64>(span, vec);
		case VK_FORMAT_R64G64_UINT: return iofmt<W, 2, u64>(span, vec);
		case VK_FORMAT_R64G64B64_UINT: return iofmt<W, 3, u64>(span, vec);
		case VK_FORMAT_R64G64B64A64_UINT: return iofmt<W, 4, u64>(span, vec);

		// NOTE: precision for 64-bit int formats can be problematic
		case VK_FORMAT_R64_SINT: return iofmt<W, 1, i64>(span, vec);
		case VK_FORMAT_R64G64_SINT: return iofmt<W, 2, i64>(span, vec);
		case VK_FORMAT_R64G64B64_SINT: return iofmt<W, 3, i64>(span, vec);
		case VK_FORMAT_R64G64B64A64_SINT: return iofmt<W, 4, i64>(span, vec);

		// packed
		case VK_FORMAT_R4G4_UNORM_PACK8: return iopack<W, FMT::UNORM, 4, 4>(span, vec);
		case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
		case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
			return iopack<W, FMT::UNORM, 5, 5, 5, 1>(span, vec);
		case VK_FORMAT_B5G6R5_UNORM_PACK16:
		case VK_FORMAT_R5G6B5_UNORM_PACK16:
			return iopack<W, FMT::UNORM, 5, 6, 5>(span, vec);
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
			return iopack<W, FMT::UNORM, 4, 4, 4, 4>(span, vec);

		case VK_FORMAT_A8B8G8R8_SINT_PACK32: return iopack<W, FMT::SINT, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return iopack<W, FMT::SRGB, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_UINT_PACK32: return iopack<W, FMT::UINT, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return iopack<W, FMT::UNORM, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return iopack<W, FMT::SNORM, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: return iopack<W, FMT::SSCALED, 8, 8, 8, 8>(span, vec);
		case VK_FORMAT_A8B8G8R8_USCALED_PACK32: return iopack<W, FMT::USCALED, 8, 8, 8, 8>(span, vec);

		case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
			return iopack<W, FMT::SNORM, 2, 10, 10, 10>(span, vec);

		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
			return iopack<W, FMT::UNORM, 2, 10, 10, 10>(span, vec);

		case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		case VK_FORMAT_A2R10G10B10_UINT_PACK32:
			return iopack<W, FMT::UINT, 2, 10, 10, 10>(span, vec);

		case VK_FORMAT_A2B10G10R10_SINT_PACK32:
		case VK_FORMAT_A2R10G10B10_SINT_PACK32:
			return iopack<W, FMT::SINT, 2, 10, 10, 10>(span, vec);

		case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
		case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
			return iopack<W, FMT::USCALED, 2, 10, 10, 10>(span, vec);

		case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
		case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
			return iopack<W, FMT::SSCALED, 2, 10, 10, 10>(span, vec);

		// depth-stencil formats.
		case VK_FORMAT_S8_UINT: return iofmt<W, 1, u8>(span, vec);
		case VK_FORMAT_D16_UNORM: return iofmt<W, 1, u16, 65535>(span, vec);
		case VK_FORMAT_D16_UNORM_S8_UINT:
			if constexpr(W) {
				write(span, u16(vec[0] * 65535));
				write(span, u8(vec[1]));
			} else {
				vec = {};
				vec[0] = read<u16>(span) / 65535.0;
				vec[1] = read<u8>(span);
			}
			break;
		case VK_FORMAT_D24_UNORM_S8_UINT:
			if constexpr(W) {
				u32 d = 16777215 * vec[0];
				write(span, u8((d >> 16) & 0xFF));
				write(span, u8((d >> 8) & 0xFF));
				write(span, u8((d) & 0xFF));
				write(span, u8(vec[1]));
			} else {
				vec = {};
				auto d = read<std::array<u8, 3>>(span);
				vec[0] = ((u32(d[0]) << 16) | (u32(d[1]) << 8) | u32(d[2])) / 16777215.0;
				vec[1] = read<u8>(span);
			}
			break;
		case VK_FORMAT_X8_D24_UNORM_PACK32:
			if constexpr(W) {
				auto fac = (1u << 24) - 1;
				u32 d = u32(fac * vec[0]);
				write(span, d);
			} else {
				vec = {};
				auto d = read<u32>(span);
				auto mask = (1u << 24) - 1;
				vec[0] = (d & mask) / double(mask);
			}
			break;
		case VK_FORMAT_D32_SFLOAT: return iofmt<W, 1, float>(span, vec);
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			if constexpr(W) {
				write(span, float(vec[0]));
				write(span, u8(vec[1]));
			} else {
				vec = {};
				vec[0] = read<float>(span);
				vec[1] = read<u8>(span);
			}
			break;

		case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
			if constexpr(W) {
				write(span, e5b9g9r9FromRgb(Vec3f(vec)));
			} else {
				vec = Vec4d(e5b9g9r9ToRgb(read<u32>(span)));
			}
			break;

		default:
			dlg_error("Format '{}' not supported for CPU reading/writing", name(format));
			break;
	}
}

template<bool W>
Vec4d formatSwizzle(Format format, Vec4d x) {
	switch(VkFormat(format)) {
		// bgra -> rgba
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SRGB:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_B8G8R8_SSCALED:
		case VK_FORMAT_B8G8R8_USCALED:
		// packed
		case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
		case VK_FORMAT_B5G6R5_UNORM_PACK16:
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
			return swizzle<W, 2, 1, 0, 3>(x);
		// (packed) abgr -> rgba
		case VK_FORMAT_A8B8G8R8_SINT_PACK32:
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		case VK_FORMAT_A8B8G8R8_UINT_PACK32:
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
		case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
		case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		case VK_FORMAT_A2B10G10R10_SINT_PACK32:
		case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
		case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
			return swizzle<W, 3, 2, 1, 0>(x);
		// (packed) argb -> rgba
		case VK_FORMAT_A2R10G10B10_SINT_PACK32:
		case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
		case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
			return swizzle<W, 1, 2, 3, 0>(x);
		default:
			return x;
	}
}

Vec4d read(Format srcFormat, span<const std::byte>& src) {
	Vec4d ret {};
	ioFormat<false>(srcFormat, src, ret);
	ret = formatSwizzle<false>(srcFormat, ret);
	return ret;
}

void write(Format dstFormat, span<std::byte>& dst, const Vec4d& color) {
	const auto sc = formatSwizzle<true>(dstFormat, color);
	ioFormat<true>(dstFormat, dst, sc);
}

void convert(Format dstFormat, span<std::byte>& dst,
		Format srcFormat, span<const std::byte>& src) {
	auto col = read(srcFormat, src);
	write(dstFormat, dst, col);
}

// Implementation directly from the OpenGL EXT_texture_shared_exponent spec
// https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/
//  d62c37dde0a40148aecc9e9701ba0ae4ab83ee22/extensions/EXT/
//  EXT_texture_shared_exponent.txt
// Notable differences: we use an endianess-agnostic implementation that
// extracts bit parts manually. Might hurt performance marginally but makes
// the implementation simpler. Also use already existent modern utility.
namespace e5b9g9r9 {
	constexpr auto expBias = 15;
	constexpr auto maxBiasedExp = 32;
	constexpr auto maxExp = maxBiasedExp - expBias;
	constexpr auto mantissaValues = 1 << 9;
	constexpr auto maxMantissa = mantissaValues - 1;
	constexpr auto max = float(maxMantissa) / mantissaValues * (1 << maxExp);

	float clamp(float x) {
		// x == NaN fails first comparison and returns 0.0
		// That's why we don't use std::clamp
		return x > 0.0 ? ((x > max) ? max : x) : 0.0;
	}

	int floorLog2(float x) {
		// int res;
		// std::frexp(x, &res);
		// return res;

		// Ok, FloorLog2 is not correct for the denorm and zero values, but we
		// are going to do a max of this value with the minimum rgb9e5 exponent
		// that will hide these problem cases.
		u32 uval;
		static_assert(sizeof(x) == sizeof(uval));
		std::memcpy(&uval, &x, sizeof(x));
		return int((uval >> 23) & 0b11111111u) - 127;
	}

} // namespace e5r9g9b9


u32 e5b9g9r9FromRgb(Vec3f rgb) {
	using namespace e5b9g9r9;
	auto rc = clamp(rgb[0]);
	auto gc = clamp(rgb[1]);
	auto bc = clamp(rgb[2]);
	auto maxrgb = std::max(rc, std::max(gc, bc));

	int expShared = std::max(0, floorLog2(maxrgb) + 1 + expBias);
	dlg_assert(expShared <= maxBiasedExp);
	dlg_assert(expShared >= 0);

	/* This pow function could be replaced by a table. */
	double denom = std::exp2(expShared - expBias - 9);
	int maxm = (int) std::floor(maxrgb / denom + 0.5);
	if(maxm == maxMantissa + 1) {
		denom *= 2;
		expShared += 1;
		dlg_assert(expShared <= maxBiasedExp);
	} else {
		dlg_assert(maxm <= maxMantissa);
	}

	auto rm = (int) std::floor(rc / denom + 0.5);
	auto gm = (int) std::floor(gc / denom + 0.5);
	auto bm = (int) std::floor(bc / denom + 0.5);

	dlg_assert(rm <= maxMantissa);
	dlg_assert(gm <= maxMantissa);
	dlg_assert(bm <= maxMantissa);
	dlg_assert(rm >= 0);
	dlg_assert(gm >= 0);
	dlg_assert(bm >= 0);

	return (expShared << 27) | (bm << 18) | (gm << 9) | rm;
}

Vec3f e5b9g9r9ToRgb(u32 ebgr) {
	using namespace e5b9g9r9;

	int exponent = int(ebgr >> 27) - int(expBias) - 9u;
  	float scale = (float) pow(2, exponent);
	return {
		scale * (ebgr & 0b111111111u),
		scale * ((ebgr >> 9) & 0b111111111u),
		scale * ((ebgr >> 18) & 0b111111111u),
	};
}

unsigned numMipLevels(const Vec2ui& extent) {
	return 1 + std::floor(std::log2(std::max(extent.x, extent.y)));
}

unsigned numMipLevels(const Vec3ui& extent) {
	auto m = std::max(extent.x, std::max(extent.y, extent.z));
	return 1 + std::floor(std::log2(m));
}

Vec3ui mipSize(const Vec3ui& size, unsigned l) {
	return {
		std::max(size.x >> l, 1u),
		std::max(size.y >> l, 1u),
		std::max(size.z >> l, 1u),
	};
}

u64 tightTexelCount(const Vec3ui& extent,
		unsigned numLayers, unsigned numMips, unsigned firstMip) {
	dlg_assert(firstMip + numMips <= numMipLevels(extent));

	u64 off = 0u;
	for(auto i = firstMip; i < firstMip + numMips; ++i) {
		auto ie = mipSize(extent, i);
		off += ie.x * ie.y * ie.z * numLayers;
	}

	return off;
}

u64 tightTexelNumber(const Vec3ui& extent,
		unsigned numLayers, unsigned mip, unsigned layer,
		unsigned x, unsigned y, unsigned z,
		unsigned firstMip) {
	dlg_assert(layer < numLayers);
	dlg_assert(mip < numMipLevels(extent));
	dlg_assert(firstMip <= mip);

	u64 off = 0u;
	for(auto i = firstMip; i < mip; ++i) {
		auto ie = mipSize(extent, i);
		off += ie.x * ie.y * ie.z * numLayers;
	}

	auto ie = mipSize(extent, mip);
	auto ltn = tightLayerTexelNumber(extent, x, y, z);
	return off + layer * ie.x * ie.y * ie.z + ltn;
}

u64 sizeBytes(Vec3ui size, u32 mip, Format fmt) {
	auto w = std::max(size.x >> mip, 1u);
	auto h = std::max(size.y >> mip, 1u);
	auto d = std::max(size.z >> mip, 1u);
	auto [bx, by, bz] = blockSize(fmt);
	w = ceilDivide(w, bx);
	h = ceilDivide(h, by);
	d = ceilDivide(d, bz);
	return w * h * d * formatElementSize(fmt);
}

} // namespace
