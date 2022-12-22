#pragma once

#include <imgio/fwd.hpp>
#include <nytl/vec.hpp>
#include <nytl/span.hpp>
#include <nytl/flags.hpp>

namespace imgio {

// Like VkFormat but we don't want a public dependency on vulkan for now
enum class Format : u32 {
	undefined = 0,
	r4g4UnormPack8 = 1,
	r4g4b4a4UnormPack16 = 2,
	b4g4r4a4UnormPack16 = 3,
	r5g6b5UnormPack16 = 4,
	b5g6r5UnormPack16 = 5,
	r5g5b5a1UnormPack16 = 6,
	b5g5r5a1UnormPack16 = 7,
	a1r5g5b5UnormPack16 = 8,
	r8Unorm = 9,
	r8Snorm = 10,
	r8Uscaled = 11,
	r8Sscaled = 12,
	r8Uint = 13,
	r8Sint = 14,
	r8Srgb = 15,
	r8g8Unorm = 16,
	r8g8Snorm = 17,
	r8g8Uscaled = 18,
	r8g8Sscaled = 19,
	r8g8Uint = 20,
	r8g8Sint = 21,
	r8g8Srgb = 22,
	r8g8b8Unorm = 23,
	r8g8b8Snorm = 24,
	r8g8b8Uscaled = 25,
	r8g8b8Sscaled = 26,
	r8g8b8Uint = 27,
	r8g8b8Sint = 28,
	r8g8b8Srgb = 29,
	b8g8r8Unorm = 30,
	b8g8r8Snorm = 31,
	b8g8r8Uscaled = 32,
	b8g8r8Sscaled = 33,
	b8g8r8Uint = 34,
	b8g8r8Sint = 35,
	b8g8r8Srgb = 36,
	r8g8b8a8Unorm = 37,
	r8g8b8a8Snorm = 38,
	r8g8b8a8Uscaled = 39,
	r8g8b8a8Sscaled = 40,
	r8g8b8a8Uint = 41,
	r8g8b8a8Sint = 42,
	r8g8b8a8Srgb = 43,
	b8g8r8a8Unorm = 44,
	b8g8r8a8Snorm = 45,
	b8g8r8a8Uscaled = 46,
	b8g8r8a8Sscaled = 47,
	b8g8r8a8Uint = 48,
	b8g8r8a8Sint = 49,
	b8g8r8a8Srgb = 50,
	a8b8g8r8UnormPack32 = 51,
	a8b8g8r8SnormPack32 = 52,
	a8b8g8r8UscaledPack32 = 53,
	a8b8g8r8SscaledPack32 = 54,
	a8b8g8r8UintPack32 = 55,
	a8b8g8r8SintPack32 = 56,
	a8b8g8r8SrgbPack32 = 57,
	a2r10g10b10UnormPack32 = 58,
	a2r10g10b10SnormPack32 = 59,
	a2r10g10b10UscaledPack32 = 60,
	a2r10g10b10SscaledPack32 = 61,
	a2r10g10b10UintPack32 = 62,
	a2r10g10b10SintPack32 = 63,
	a2b10g10r10UnormPack32 = 64,
	a2b10g10r10SnormPack32 = 65,
	a2b10g10r10UscaledPack32 = 66,
	a2b10g10r10SscaledPack32 = 67,
	a2b10g10r10UintPack32 = 68,
	a2b10g10r10SintPack32 = 69,
	r16Unorm = 70,
	r16Snorm = 71,
	r16Uscaled = 72,
	r16Sscaled = 73,
	r16Uint = 74,
	r16Sint = 75,
	r16Sfloat = 76,
	r16g16Unorm = 77,
	r16g16Snorm = 78,
	r16g16Uscaled = 79,
	r16g16Sscaled = 80,
	r16g16Uint = 81,
	r16g16Sint = 82,
	r16g16Sfloat = 83,
	r16g16b16Unorm = 84,
	r16g16b16Snorm = 85,
	r16g16b16Uscaled = 86,
	r16g16b16Sscaled = 87,
	r16g16b16Uint = 88,
	r16g16b16Sint = 89,
	r16g16b16Sfloat = 90,
	r16g16b16a16Unorm = 91,
	r16g16b16a16Snorm = 92,
	r16g16b16a16Uscaled = 93,
	r16g16b16a16Sscaled = 94,
	r16g16b16a16Uint = 95,
	r16g16b16a16Sint = 96,
	r16g16b16a16Sfloat = 97,
	r32Uint = 98,
	r32Sint = 99,
	r32Sfloat = 100,
	r32g32Uint = 101,
	r32g32Sint = 102,
	r32g32Sfloat = 103,
	r32g32b32Uint = 104,
	r32g32b32Sint = 105,
	r32g32b32Sfloat = 106,
	r32g32b32a32Uint = 107,
	r32g32b32a32Sint = 108,
	r32g32b32a32Sfloat = 109,
	r64Uint = 110,
	r64Sint = 111,
	r64Sfloat = 112,
	r64g64Uint = 113,
	r64g64Sint = 114,
	r64g64Sfloat = 115,
	r64g64b64Uint = 116,
	r64g64b64Sint = 117,
	r64g64b64Sfloat = 118,
	r64g64b64a64Uint = 119,
	r64g64b64a64Sint = 120,
	r64g64b64a64Sfloat = 121,
	b10g11r11UfloatPack32 = 122,
	e5b9g9r9UfloatPack32 = 123,
	d16Unorm = 124,
	x8D24UnormPack32 = 125,
	d32Sfloat = 126,
	s8Uint = 127,
	d16UnormS8Uint = 128,
	d24UnormS8Uint = 129,
	d32SfloatS8Uint = 130,
	bc1RgbUnormBlock = 131,
	bc1RgbSrgbBlock = 132,
	bc1RgbaUnormBlock = 133,
	bc1RgbaSrgbBlock = 134,
	bc2UnormBlock = 135,
	bc2SrgbBlock = 136,
	bc3UnormBlock = 137,
	bc3SrgbBlock = 138,
	bc4UnormBlock = 139,
	bc4SnormBlock = 140,
	bc5UnormBlock = 141,
	bc5SnormBlock = 142,
	bc6hUfloatBlock = 143,
	bc6hSfloatBlock = 144,
	bc7UnormBlock = 145,
	bc7SrgbBlock = 146,
	etc2R8g8b8UnormBlock = 147,
	etc2R8g8b8SrgbBlock = 148,
	etc2R8g8b8a1UnormBlock = 149,
	etc2R8g8b8a1SrgbBlock = 150,
	etc2R8g8b8a8UnormBlock = 151,
	etc2R8g8b8a8SrgbBlock = 152,
	eacR11UnormBlock = 153,
	eacR11SnormBlock = 154,
	eacR11g11UnormBlock = 155,
	eacR11g11SnormBlock = 156,
	astc4x4UnormBlock = 157,
	astc4x4SrgbBlock = 158,
	astc5x4UnormBlock = 159,
	astc5x4SrgbBlock = 160,
	astc5x5UnormBlock = 161,
	astc5x5SrgbBlock = 162,
	astc6x5UnormBlock = 163,
	astc6x5SrgbBlock = 164,
	astc6x6UnormBlock = 165,
	astc6x6SrgbBlock = 166,
	astc8x5UnormBlock = 167,
	astc8x5SrgbBlock = 168,
	astc8x6UnormBlock = 169,
	astc8x6SrgbBlock = 170,
	astc8x8UnormBlock = 171,
	astc8x8SrgbBlock = 172,
	astc10x5UnormBlock = 173,
	astc10x5SrgbBlock = 174,
	astc10x6UnormBlock = 175,
	astc10x6SrgbBlock = 176,
	astc10x8UnormBlock = 177,
	astc10x8SrgbBlock = 178,
	astc10x10UnormBlock = 179,
	astc10x10SrgbBlock = 180,
	astc12x10UnormBlock = 181,
	astc12x10SrgbBlock = 182,
	astc12x12UnormBlock = 183,
	astc12x12SrgbBlock = 184,
	g8b8g8r8422Unorm = 1000156000,
	b8g8r8g8422Unorm = 1000156001,
	g8B8R83plane420Unorm = 1000156002,
	g8B8r82plane420Unorm = 1000156003,
	g8B8R83plane422Unorm = 1000156004,
	g8B8r82plane422Unorm = 1000156005,
	g8B8R83plane444Unorm = 1000156006,
	r10x6UnormPack16 = 1000156007,
	r10x6g10x6Unorm2pack16 = 1000156008,
	r10x6g10x6b10x6a10x6Unorm4pack16 = 1000156009,
	g10x6b10x6g10x6r10x6422Unorm4pack16 = 1000156010,
	b10x6g10x6r10x6g10x6422Unorm4pack16 = 1000156011,
	g10x6B10x6R10x63plane420Unorm3pack16 = 1000156012,
	g10x6B10x6r10x62plane420Unorm3pack16 = 1000156013,
	g10x6B10x6R10x63plane422Unorm3pack16 = 1000156014,
	g10x6B10x6r10x62plane422Unorm3pack16 = 1000156015,
	g10x6B10x6R10x63plane444Unorm3pack16 = 1000156016,
	r12x4UnormPack16 = 1000156017,
	r12x4g12x4Unorm2pack16 = 1000156018,
	r12x4g12x4b12x4a12x4Unorm4pack16 = 1000156019,
	g12x4b12x4g12x4r12x4422Unorm4pack16 = 1000156020,
	b12x4g12x4r12x4g12x4422Unorm4pack16 = 1000156021,
	g12x4B12x4R12x43plane420Unorm3pack16 = 1000156022,
	g12x4B12x4r12x42plane420Unorm3pack16 = 1000156023,
	g12x4B12x4R12x43plane422Unorm3pack16 = 1000156024,
	g12x4B12x4r12x42plane422Unorm3pack16 = 1000156025,
	g12x4B12x4R12x43plane444Unorm3pack16 = 1000156026,
	g16b16g16r16422Unorm = 1000156027,
	b16g16r16g16422Unorm = 1000156028,
	g16B16R163plane420Unorm = 1000156029,
	g16B16r162plane420Unorm = 1000156030,
	g16B16R163plane422Unorm = 1000156031,
	g16B16r162plane422Unorm = 1000156032,
	g16B16R163plane444Unorm = 1000156033,
	pvrtc12bppUnormBlockIMG = 1000054000,
	pvrtc14bppUnormBlockIMG = 1000054001,
	pvrtc22bppUnormBlockIMG = 1000054002,
	pvrtc24bppUnormBlockIMG = 1000054003,
	pvrtc12bppSrgbBlockIMG = 1000054004,
	pvrtc14bppSrgbBlockIMG = 1000054005,
	pvrtc22bppSrgbBlockIMG = 1000054006,
	pvrtc24bppSrgbBlockIMG = 1000054007,
	astc4x4SfloatBlockEXT = 1000066000,
	astc5x4SfloatBlockEXT = 1000066001,
	astc5x5SfloatBlockEXT = 1000066002,
	astc6x5SfloatBlockEXT = 1000066003,
	astc6x6SfloatBlockEXT = 1000066004,
	astc8x5SfloatBlockEXT = 1000066005,
	astc8x6SfloatBlockEXT = 1000066006,
	astc8x8SfloatBlockEXT = 1000066007,
	astc10x5SfloatBlockEXT = 1000066008,
	astc10x6SfloatBlockEXT = 1000066009,
	astc10x8SfloatBlockEXT = 1000066010,
	astc10x10SfloatBlockEXT = 1000066011,
	astc12x10SfloatBlockEXT = 1000066012,
	astc12x12SfloatBlockEXT = 1000066013,
	g8b8g8r8422UnormKHR = 1000156000,
	b8g8r8g8422UnormKHR = 1000156001,
	g8B8R83plane420UnormKHR = 1000156002,
	g8B8r82plane420UnormKHR = 1000156003,
	g8B8R83plane422UnormKHR = 1000156004,
	g8B8r82plane422UnormKHR = 1000156005,
	g8B8R83plane444UnormKHR = 1000156006,
	r10x6UnormPack16KHR = 1000156007,
	r10x6g10x6Unorm2pack16KHR = 1000156008,
	r10x6g10x6b10x6a10x6Unorm4pack16KHR = 1000156009,
	g10x6b10x6g10x6r10x6422Unorm4pack16KHR = 1000156010,
	b10x6g10x6r10x6g10x6422Unorm4pack16KHR = 1000156011,
	g10x6B10x6R10x63plane420Unorm3pack16KHR = 1000156012,
	g10x6B10x6r10x62plane420Unorm3pack16KHR = 1000156013,
	g10x6B10x6R10x63plane422Unorm3pack16KHR = 1000156014,
	g10x6B10x6r10x62plane422Unorm3pack16KHR = 1000156015,
	g10x6B10x6R10x63plane444Unorm3pack16KHR = 1000156016,
	r12x4UnormPack16KHR = 1000156017,
	r12x4g12x4Unorm2pack16KHR = 1000156018,
	r12x4g12x4b12x4a12x4Unorm4pack16KHR = 1000156019,
	g12x4b12x4g12x4r12x4422Unorm4pack16KHR = 1000156020,
	b12x4g12x4r12x4g12x4422Unorm4pack16KHR = 1000156021,
	g12x4B12x4R12x43plane420Unorm3pack16KHR = 1000156022,
	g12x4B12x4r12x42plane420Unorm3pack16KHR = 1000156023,
	g12x4B12x4R12x43plane422Unorm3pack16KHR = 1000156024,
	g12x4B12x4r12x42plane422Unorm3pack16KHR = 1000156025,
	g12x4B12x4R12x43plane444Unorm3pack16KHR = 1000156026,
	g16b16g16r16422UnormKHR = 1000156027,
	b16g16r16g16422UnormKHR = 1000156028,
	g16B16R163plane420UnormKHR = 1000156029,
	g16B16r162plane420UnormKHR = 1000156030,
	g16B16R163plane422UnormKHR = 1000156031,
	g16B16r162plane422UnormKHR = 1000156032,
	g16B16R163plane444UnormKHR = 1000156033,
	a4r4g4b4UnormPack16EXT = 1000340000,
	a4b4g4r4UnormPack16EXT = 1000340001
};

// Like VkImageAspectFlagBits but we don't want a direct dependency on vulkan for now
enum class FormatAspect : u32 {
    color = 0x00000001,
    depth = 0x00000002,
    stencil = 0x00000004,
    metadata = 0x00000008,
    plane0 = 0x00000010,
    plane1 = 0x00000020,
    plane2 = 0x00000040,
};

NYTL_FLAG_OPS(FormatAspect)

u32 formatElementSize(Format);
u32 formatElementSize(Format, FormatAspect);
Vec3ui blockSize(Format);
bool isSRGB(Format);
Format toggleSRGB(Format format);

// Returns the number of bytes needed to store an single face/layer
// of an image with the given size and format, in the mip level.
// 'size' is the size of the full image (level 0), not the size of the
// mip subresource.
u64 sizeBytes(Vec3ui size, u32 mip, Format fmt);

// NOTE: rgb should be in linear space
u32 e5b9g9r9FromRgb(Vec3f rgb);
Vec3f e5b9g9r9ToRgb(u32 e5r9g9b9);

// Limitations of format I/O:
// - No multiple formats
// - No block-compressed formats
// - No support for Format::b10g11r11UfloatPack32
Vec4d read(Format srcFormat, span<const std::byte>& src);
void write(Format dstFormat, span<std::byte>& dst, const Vec4d& color);
void convert(Format dstFormat, span<std::byte>& dst,
		Format srcFormat, span<const std::byte>& src);

// does the correct conversion, no pow(2.2) approximation
double linearToSRGB(double linear);
double srgbToLinear(double srgb);

// preserves alpha component, as per vulkan
Vec4d linearToSRGB(Vec4d);
Vec4d srgbToLinear(Vec4d);

/// Returns the number of mipmap levels needed for a full mipmap
/// chain for an image with the given extent.
[[nodiscard]] unsigned numMipLevels(const Vec2ui& extent);
[[nodiscard]] unsigned numMipLevels(const Vec3ui& extent);

/// Returns the size of an image with given size at the given mip level.
/// Returns {1, 1, 1} if the mip level does not exist (i.e. too high).
[[nodiscard]] Vec3ui mipSize(const Vec3ui& size, unsigned l);

/// Returns the pixel number of a given texel in a tightly, linear layout
/// image, dimension order: mips, layers, depth, height, width.
/// To obtain the buffer offset, multiply the address with the format size
/// of the image.
/// Mainly interesting for images with multiple mip levels since the
/// offset formula is not trivial in that case (since each level
/// has different size).
/// This cannot be used to compute the address of a texel in a linear
/// vulkan image, see vpp::texelAddress for that (above).
/// - extent: size of the linear image
/// - numLayers: number of layers the image has
/// - mip: the mip of the texel to compute the texel number for.
///   `mip < mipmapLevels(extent)` must hold, i.e. if the
///   mip level can't exist for the given extent
/// - layer: the layer of the texel to compute the texel number for
///   `layer < numLayers` must hold.
/// - x,y,z: coordinates of the texel in the given mip, layer
/// - firstMip: which mip begins at texel number 0.
///   `firstMip <= mip` must hold. Note that 'mip' is not relative
///   to baseMip but absolute.
[[nodiscard]] u64 tightTexelNumber(const Vec3ui& extent,
	unsigned numLayers, unsigned mip, unsigned layer,
	unsigned x = 0u, unsigned y = 0u, unsigned z = 0u,
	unsigned firstMip = 0u);

/// Returns the number of texels in the specified image subresource range
/// in a tight, linear layout.
[[nodiscard]] u64 tightTexelCount(const Vec3ui& extent,
	unsigned numLayers, unsigned numMips, unsigned firstMip = 0u);

/// Equivalent to tightTexelNumber(extent, 1, 0, 0, x, y, z),
/// i.e. returns the texel number in a single layer for an image
/// of the given size.
[[nodiscard]] inline u64 tightLayerTexelNumber(const Vec3ui& extent,
		unsigned x, unsigned y, unsigned z = 0) {
	return z * (extent.y * extent.x) + y * extent.x + x;
}

} // namespace
