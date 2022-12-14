#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/f16.hpp>
#include <imgio/format.hpp>
#include <nytl/scope.hpp>
#include <nytl/stringParam.hpp>
#include <dlg/dlg.hpp>
#include <vector>
#include <optional>

#define TINYEXR_IMPLEMENTATION
#include <zlib.h>
#include "../tinyexr.hpp"

// TODO: extend support, evaluate what is needed/useful:
// - support for actually tiled images?
//   also, support for mip level is not tested yet, since I didn't
//   found an example file that wasn't *really* tiled.
// - support for multipart images
// - support for deep images?
// TODO: remove unneeded high-level functions from tinyexr.
//   should reduce it by quite some size.
// TODO: add 'class ExrReader : ImageProvider'. Make it a public
//   interface. This can supply additional attributes, channels,
//   allow loading deep images. Then we can also use the StreamMemoryMap
//   optimization instead of always reading the whole image,
//   should help performance a lot.

// technical source/specificiation:
//   https://www.openexr.com/documentation/TechnicalIntroduction.pdf
// lots of good example images:
//   https://github.com/AcademySoftwareFoundation/openexr-images
// Supporting all images in MultiResolution shouldn't be too hard.
// We already support MultiView (the rgb ones)

namespace imgio {

inline std::pair<std::string_view, std::string_view> split(
		std::string_view src, std::string_view::size_type pos) {
	dlg_assert(pos != src.npos && pos < src.size());
	auto first = src;
	auto second = src;
	second.remove_prefix(pos + 1);
	first.remove_suffix(src.size() - pos);
	return {first, second};
}

ReadError toReadError(int res) {
	switch(res) {
		case TINYEXR_SUCCESS:
			return ReadError::none;
		case TINYEXR_ERROR_INVALID_MAGIC_NUMBER:
		case TINYEXR_ERROR_INVALID_EXR_VERSION:
		case TINYEXR_ERROR_INVALID_FILE:
		case TINYEXR_ERROR_INVALID_DATA:
		case TINYEXR_ERROR_INVALID_HEADER:
			return ReadError::invalidType;
		case TINYEXR_ERROR_UNSUPPORTED_FORMAT:
			return ReadError::unsupportedFormat;
		case TINYEXR_ERROR_UNSUPPORTED_FEATURE:
			return ReadError::cantRepresent;
		default:
			return ReadError::internal;
	}
}

// NOTE: we return formats in order RGBA. We could also return ABGR or
// any other order but RGBA is probably the most common and widest
// supported one.

constexpr auto noChannel = u32(0xFFFFFFFF);
Format parseFormat(const std::array<u32, 4>& mapping, int exrPixelType,
		bool forceRGBA) {
	auto maxChan = forceRGBA ? 3 :
		mapping[3] != noChannel ? 3 :
		mapping[2] != noChannel ? 2 :
		mapping[1] != noChannel ? 1 : 0;
	switch(maxChan) {
		case 0:
			switch(exrPixelType) {
				case TINYEXR_PIXELTYPE_UINT: return Format::r32Uint;
				case TINYEXR_PIXELTYPE_HALF: return Format::r16Sfloat;
				case TINYEXR_PIXELTYPE_FLOAT: return Format::r32Sfloat;
				default: return Format::undefined;
			}
		case 1:
			switch(exrPixelType) {
				case TINYEXR_PIXELTYPE_UINT: return Format::r32g32Uint;
				case TINYEXR_PIXELTYPE_HALF: return Format::r16g16Sfloat;
				case TINYEXR_PIXELTYPE_FLOAT: return Format::r32g32Sfloat;
				default: return Format::undefined;
			}
		case 2:
			switch(exrPixelType) {
				case TINYEXR_PIXELTYPE_UINT: return Format::r32g32b32Uint;
				case TINYEXR_PIXELTYPE_HALF: return Format::r16g16b16Sfloat;
				case TINYEXR_PIXELTYPE_FLOAT: return Format::r32g32b32Sfloat;
				default: return Format::undefined;
			}
		case 3:
			switch(exrPixelType) {
				case TINYEXR_PIXELTYPE_UINT: return Format::r32g32b32a32Uint;
				case TINYEXR_PIXELTYPE_HALF: return Format::r16g16b16a16Sfloat;
				case TINYEXR_PIXELTYPE_FLOAT: return Format::r32g32b32a32Sfloat;
				default: return Format::undefined;
			}
		default: return Format::undefined;
	}
}

ReadError loadExr(std::unique_ptr<Read>&& stream,
		std::unique_ptr<ImageProvider>& provider, bool forceRGBA) {
	dlg_debug("== Loading EXR image ==");

	// TODO: could optimize this significantly.
	// When it's a memory stream, just use the memory.
	// When it's a file stream and we're on linux, try to map it.
	stream->seek(0, Seek::Origin::end);
	auto size = stream->address();
	stream->seek(0);

	std::unique_ptr<std::byte[]> buf = std::make_unique<std::byte[]>(size);
	auto* data = reinterpret_cast<const unsigned char*>(buf.get());
	stream->read(buf.get(), size);

	EXRVersion version;
	auto res = ParseEXRVersionFromMemory(&version, data, size);
	if(res != TINYEXR_SUCCESS) {
		dlg_debug("ParseEXRVersionFromMemory: {}", res);
		return toReadError(res);
	}

	dlg_debug("EXR image information:");
	dlg_debug("  version: {}", version.version);
	dlg_debug("  tiled: {}", version.tiled);
	dlg_debug("  long_name: {}", version.long_name);
	dlg_debug("  non_image: {}", version.non_image);
	dlg_debug("  multipart: {}", version.multipart);

	if(version.non_image) {
		dlg_warn("EXR deep images not supported");
		return ReadError::cantRepresent;
	}

	if(version.multipart) {
		dlg_warn("EXR multipart images not supported");
		return ReadError::cantRepresent;
	}

	const char* err {};
	EXRHeader header {};
	res = ParseEXRHeaderFromMemory(&header, &version, data, size, &err);
	if(res != TINYEXR_SUCCESS) {
		dlg_debug("ParseEXRHeaderFrommemory: {} ({})", err ? err : "-", res);
		FreeEXRErrorMessage(err);
		return toReadError(res);
	}

	auto headerGuard = ScopeGuard([&]{ FreeEXRHeader(&header); });
	dlg_assert(header.tiled == version.tiled);
	dlg_assert(header.multipart == version.multipart);
	dlg_assert(header.non_image == version.non_image);
	if(header.tiled) {
		// ripmap basically means mipmaps in both dimensions independently.
		// We can't represent that via ImageProvider (and it's not
		// really needed in gpu rendering I guess).
		if(header.tile_level_mode == TINYEXR_TILE_RIPMAP_LEVELS) {
			dlg_warn("EXR discarding non-mipmap ripmap levels");
		}

		// sadly this is the default for vulkan. atm there is only
		// an nvidia extension for up-rounding mip map sizes
		// NOTE: we could choose to just ignore the mipmaps here
		// instead of completely failing the load process.
		if(header.tile_rounding_mode != TINYEXR_TILE_ROUND_DOWN) {
			dlg_warn("EXR invalid mip rounding mode {}", header.tile_rounding_mode);
			return ReadError::cantRepresent;
		}
	}

	for(auto i = 0u; i < unsigned(header.num_custom_attributes); ++i) {
		auto& att = header.custom_attributes[i];
		dlg_debug("attribute {} (type {}, size {})", att.name, att.type, att.size);
	}

	struct Layer {
		std::string_view name;
		std::array<u32, 4> mapping {noChannel, noChannel, noChannel, noChannel};
	};

	std::vector<Layer> layers;
	std::optional<int> oPixelType;

	for(auto i = 0u; i < unsigned(header.num_channels); ++i) {
		std::string_view name = header.channels[i].name;
		dlg_debug("channel {}: {}", i, name);

		std::string_view layerName, channelName;
		auto sepos = name.find_last_of('.');
		if(sepos == std::string::npos) {
			// default layer.
			// This means we will interpret ".R" and "R" the same,
			// an image that has both can't be parsed.
			layerName = "";
			channelName = name;
		} else {
			std::tie(layerName, channelName) = split(name, sepos);
		}

		unsigned id;
		if(channelName == "R") id = 0;
		else if(channelName == "G") id = 1;
		else if(channelName == "B") id = 2;
		else if(channelName == "A") id = 3;
		else {
			dlg_info(" Ignoring unknown channel {}", channelName);
			continue;
		}

		auto it = std::find_if(layers.begin(), layers.end(),
			[&](auto& layer) { return layer.name == layerName; });
		if(it == layers.end()) {
			layers.emplace_back().name = layerName;
			it = layers.end() - 1;
		}

		if(it->mapping[id] != noChannel) {
			dlg_warn("EXR layer has multiple {} channels", name);
			return ReadError::unsupportedFormat;
		}

		it->mapping[id] = i;
		if(!oPixelType) {
			oPixelType = header.channels[i].pixel_type;
		} else {
			// all known (rgba) channels must have the same pixel type,
			// there are no formats with varying types.
			if(header.channels[i].pixel_type != *oPixelType) {
				dlg_warn("EXR image channels have different pixel types");
				return ReadError::unsupportedFormat;
			}
		}
	}

	if(layers.empty()) {
		dlg_error("EXR image has no channels/layers");
		return ReadError::empty;
	}

	auto pixelType = *oPixelType;

	std::optional<Format> oFormat;
	for(auto it = layers.begin(); it != layers.end();) {
		auto iformat = parseFormat(it->mapping, pixelType, forceRGBA);
		if((oFormat && *oFormat != iformat) || iformat == Format::undefined) {
			dlg_warn("EXR image layer {} has {} format, ignoring it",
				oFormat ? "different" : "invalid", it - layers.begin());
			it = layers.erase(it);
			continue;
		}

		oFormat = iformat;
		++it;
	}

	if(!oFormat) {
		dlg_warn("EXR image has no layer with parsable format");
		return ReadError::empty;
	}

	auto format = *oFormat;

	EXRImage image {};
	res = LoadEXRImageFromMemory(&image, &header, data, size, &err);
	if(res != TINYEXR_SUCCESS) {
		dlg_debug("LoadEXRImageFromMemory: {} ({})", err ? err : "-", res);
		FreeEXRErrorMessage(err);
		return toReadError(res);
	}

	auto imageGuard = ScopeGuard([&]{ FreeEXRImage(&image); });

	unsigned width = image.width;
	unsigned height = image.height;
	dlg_assert(image.num_channels == header.num_channels);
	std::vector<unsigned char**> mips;

	dlg_debug("EXR width: {}, height {}", width, height);
	if(header.tiled) {
		if(!image.tiles) {
			dlg_warn("Reading EXRImage returned no data");
			return ReadError::internal;
		}

		// TODO: we could support cubemaps, image.height == 6 * header.tile_size.y
		// They additional have the 'envmap' tag set (to 1) to signal that
		// this exr represents a cubemap.
		if(header.tile_size_x != image.width || header.tile_size_y != image.height) {
			dlg_warn("EXR actually tiled images not supported");
			return ReadError::cantRepresent;
		}

		auto numLevels = numMipLevels({width, height, 1u});
		mips.reserve(numLevels);
		for(auto i = 0u; i < u32(image.num_tiles); ++i) {
			auto& tile = image.tiles[i];

			// dlg_debug("tile {}: lvl: {}, {} offset: {}, {} size: {}, {}", i,
			// 	tile.level_x, tile.level_y, tile.offset_x, tile.offset_y,
			// 	tile.width, tile.height);
			// continue;

			if(tile.offset_x != 0 || tile.offset_y != 0) {
				// NOTE: we could support it but it's probably not
				// worth the effort, I don't see the point of storing
				// images as actual tiles.
				dlg_warn("EXR actually tiled images not supported. Offset {} {}",
					tile.offset_x, tile.offset_y);
				return ReadError::cantRepresent;
			}

			// we discard ripmap levels
			if(tile.level_x != tile.level_y) {
				continue;
			}

			auto lvl = tile.level_x;
			if(lvl >= i32(numLevels) || lvl < 0) {
				dlg_warn("EXR invalid number of levels");
				return ReadError::cantRepresent;
			}

			auto ewidth = i32(std::max(width >> lvl, 1u));
			auto eheight = i32(std::max(height >> lvl, 1u));
			dlg_assertm(tile.width == ewidth, "{} vs {}", tile.width, ewidth);
			dlg_assertm(tile.height == eheight, "{} vs {}", tile.height, eheight);

			mips.resize(std::max<u32>(mips.size(), lvl));
			dlg_assertm(!mips[lvl], "EXR image has duplicate mip level {}", lvl);
			mips[lvl] = tile.images;
		}

		// EXR specifies there must be a full mip chain.
		// We can't trust the data if there is not
		if(mips.size() != numLevels) {
			dlg_warn("EXR image has invalid number of levels");
			return ReadError::cantRepresent;
		}
	} else {
		if(!image.images) {
			dlg_warn("Reading EXRImage returned no data");
			return ReadError::internal;
		}

		mips.push_back(image.images);
	}


	// interlace channels
	auto fmtSize = formatElementSize(format);
	auto totalSize = fmtSize * tightTexelNumber(
		{width, height, 1u}, layers.size() + 1, mips.size() + 1, 0u);
	auto interlaced = std::make_unique<std::byte[]>(totalSize);

	auto chanSize = pixelType == TINYEXR_PIXELTYPE_HALF ?  2 : 4;

	std::byte src1[4];
	if(pixelType == TINYEXR_PIXELTYPE_HALF) {
		auto src = f16(1.f);
		std::memcpy(src1, &src, sizeof(src));
	} else if(pixelType == TINYEXR_PIXELTYPE_UINT) {
		auto src = u32(1);
		std::memcpy(src1, &src, sizeof(src));
	} else if(pixelType == TINYEXR_PIXELTYPE_FLOAT) {
		auto src = float(1.f);
		std::memcpy(src1, &src, sizeof(src));
	}

	// NOTE: instead of just forceRGBA, we could already perform
	// cpu format conversion here if this is desired (i.e. the required
	// format different than what we desire). Could extent the api
	// to allow this.
	for(auto m = 0u; m < mips.size(); ++m) {
		auto iwidth = std::max(width >> m, 1u);
		auto iheight = std::max(height >> m, 1u);
		auto srcData = mips[m];

		for(auto l = 0u; l < layers.size(); ++l) {
			auto& layer = layers[l];
			auto dstOff = fmtSize * tightTexelNumber(
				{width, height, 1u}, layers.size(), m, l);

			for(auto y = 0u; y < iheight; ++y) {
				for(auto x = 0u; x < iwidth; ++x) {
					auto address = y * iwidth + x;
					auto dst = interlaced.get() + dstOff + (address * fmtSize);

					for(auto c = 0u; c < 4u; ++c) {
						auto id = layer.mapping[c];
						if(id == noChannel) {
							std::memcpy(dst + c * chanSize, src1, chanSize);
						} else {
							auto src = srcData[id] + chanSize * address;
							std::memcpy(dst + c * chanSize, src, chanSize);
						}
					}
				}
			}
		}
	}

	dlg_debug("== EXR image loading success ==");
	provider = wrapImage({width, height, 1u}, format, mips.size(), layers.size(),
		std::move(interlaced));
	return provider ? ReadError::none : ReadError::internal;
}

WriteError writeExr(StringParam path, const ImageProvider& provider) {
	auto [width, height, depth] = provider.size();
	if(depth > 1) {
		dlg_warn("writeExr: discarding {} slices", depth - 1);
	}

	if(provider.mipLevels() > 1) {
		dlg_warn("writeExr: discarding {} mips", provider.mipLevels() - 1);
	}

	// TODO: we could add support for writing multiple layers
	if(provider.layers() > 1) {
		dlg_warn("writeExr: discarding {} layers", provider.layers() - 1);
	}

	EXRChannelInfo channels[4] {};

	auto fmt = provider.format();
	auto nc = 0u;
	unsigned chanSize;
	int pixType;
	switch(fmt) {
		case Format::r16g16b16a16Sfloat:
			std::strcpy(channels[nc++].name, "A"); [[fallthrough]];
		case Format::r16g16b16Sfloat:
			std::strcpy(channels[nc++].name, "B"); [[fallthrough]];
		case Format::r16g16Sfloat:
			std::strcpy(channels[nc++].name, "G"); [[fallthrough]];
		case Format::r16Sfloat:
			chanSize = 2;
			pixType = TINYEXR_PIXELTYPE_HALF;
			std::strcpy(channels[nc++].name, "R");
			break;

		case Format::r32g32b32a32Sfloat:
			std::strcpy(channels[nc++].name, "A"); [[fallthrough]];
		case Format::r32g32b32Sfloat:
			std::strcpy(channels[nc++].name, "B"); [[fallthrough]];
		case Format::r32g32Sfloat:
			std::strcpy(channels[nc++].name, "G"); [[fallthrough]];
		case Format::r32Sfloat:
			chanSize = 4;
			pixType = TINYEXR_PIXELTYPE_FLOAT;
			std::strcpy(channels[nc++].name, "R");
			break;

		case Format::r32g32b32a32Uint:
			std::strcpy(channels[nc++].name, "A"); [[fallthrough]];
		case Format::r32g32b32Uint:
			std::strcpy(channels[nc++].name, "B"); [[fallthrough]];
		case Format::r32g32Uint:
			std::strcpy(channels[nc++].name, "G"); [[fallthrough]];
		case Format::r32Uint:
			chanSize = 4;
			pixType = TINYEXR_PIXELTYPE_UINT;
			std::strcpy(channels[nc++].name, "R");
			break;

		default:
			dlg_error("Can't represent format {} as exr", (int) fmt);
			return WriteError::unsupportedFormat;
	}

	int pixTypes[4] {pixType, pixType, pixType, pixType};

	EXRHeader header {};
	header.channels = channels;
	header.num_channels = nc;
	header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;
	header.pixel_types = pixTypes;
	header.requested_pixel_types = pixTypes;

	auto pixelSize = formatElementSize(fmt);
	auto byteSize = width * height * pixelSize;

	auto data = provider.read();
	if(data.size() != byteSize) {
		dlg_warn("writeExr: expected {} bytes from provider, got {}",
			byteSize, data.size());
		return WriteError::readError;
	}

	// de-interlace
	auto deint = std::make_unique<std::byte[]>(byteSize);
	unsigned char* chanptrs[4];

	auto planeSize = width * height * chanSize;
	for(auto c = 0u; c < nc; ++c) {
		auto base = deint.get() + c * planeSize;
		for(auto y = 0u; y < height; ++y) {
			for(auto x = 0u; x < width; ++x) {
				auto address = y * width + x;
				auto src = data.data() + address * pixelSize + c * chanSize;
				auto dst = base + address * chanSize;
				std::memcpy(dst, src, chanSize);
			}
		}

		// channel order is reversed, see the switch above
		chanptrs[nc - c - 1] = reinterpret_cast<unsigned char*>(base);
	}

	EXRImage img {};
	img.width = width;
	img.height = height;
	img.images = chanptrs;
	img.num_channels = nc;

	const char* err {};
	auto res = SaveEXRImageToFile(&img, &header, path.c_str(), &err);
	if(res != TINYEXR_SUCCESS) {
		dlg_debug("SaveEXRImagetofile: {} ({})", err ? err : "-", res);
		FreeEXRErrorMessage(err);
		switch(res) {
			case TINYEXR_ERROR_CANT_OPEN_FILE:
				return WriteError::cantOpen;
			case TINYEXR_ERROR_CANT_WRITE_FILE:
				return WriteError::cantWrite;
			case TINYEXR_ERROR_UNSUPPORTED_FORMAT:
				return WriteError::unsupportedFormat;
			default:
				return WriteError::internal;
		}
	}

	return WriteError::none;
}

} // namespace
