#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/file.hpp>
#include <imgio/format.hpp>
#include <dlg/dlg.hpp>

#include <png.h>
#include <csetjmp>
#include <cstdio>
#include <vector>

namespace imgio {

class PngReader : public ImageProvider {
public:
	std::unique_ptr<Read> stream_ {};
	Vec2ui size_;
	png_infop pngInfo_ {};
	png_structp png_ {};
	Format format_ {};
	mutable std::vector<std::byte> tmpData_ {};

public:
	~PngReader() {
		if(png_) {
			::png_destroy_read_struct(&png_, &pngInfo_, nullptr);
		}
	}

	Vec3ui size() const noexcept override { return {size_.x, size_.y, 1u}; }
	Format format() const noexcept override { return format_; }

	u64 read(nytl::Span<std::byte> data, unsigned mip, unsigned layer) const override {
		dlg_assert(mip == 0);
		dlg_assert(layer == 0);

		auto byteSize = size_.x * size_.y * formatElementSize(format());
		dlg_assert(data.size() >= byteSize);

		auto rows = std::make_unique<png_bytep[]>(size_.y);
		if(::setjmp(png_jmpbuf(png_))) {
			throw std::runtime_error("setjmp(png_jmpbuf) failed");
		}

		auto rowSize = png_get_rowbytes(png_, pngInfo_);
		dlg_assert(rowSize == size_.x * formatElementSize(format()));

		auto ptr = reinterpret_cast<unsigned char*>(data.data());
		for(auto y = 0u; y < size_.y; ++y) {
			rows[y] = ptr + rowSize * y;
		}

		png_read_image(png_, rows.get());
		return byteSize;
	}

	nytl::Span<const std::byte> read(unsigned mip, unsigned layer) const override {
		auto byteSize = size_.x * size_.y * formatElementSize(format());
		tmpData_.resize(byteSize);
		auto res = read(tmpData_, mip, layer);
		dlg_assert(res == byteSize);
		return tmpData_;
	}
};

void readPngDataFromStream(png_structp png_ptr, png_bytep outBytes,
		png_size_t byteCountToRead) {
	png_voidp io_ptr = png_get_io_ptr(png_ptr);
	dlg_assert(io_ptr);

	Read& stream = *(Read*) io_ptr;
	auto res = stream.readPartial((std::byte*) outBytes, byteCountToRead);
	dlg_assert(res == i64(byteCountToRead));
}

ReadError loadPng(std::unique_ptr<Read>&& stream, PngReader& reader) {
	unsigned char sig[8];
	if(!stream->readPartial(sig)) {
		return ReadError::unexpectedEnd;
	}

	if(::png_sig_cmp(sig, 0, sizeof(sig))) {
    	return ReadError::invalidType;
  	}

	reader.png_ = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if(!reader.png_) {
    	return ReadError::invalidType;
	}

	reader.pngInfo_ = png_create_info_struct(reader.png_);
	if(!reader.pngInfo_) {
    	return ReadError::invalidType;
	}

	if(::setjmp(png_jmpbuf(reader.png_))) {
		return ReadError::internal;
	}

	png_set_read_fn(reader.png_, stream.get(), readPngDataFromStream);
	png_set_sig_bytes(reader.png_, sizeof(sig));
	png_read_info(reader.png_, reader.pngInfo_);

	reader.size_.x = png_get_image_width(reader.png_, reader.pngInfo_);
	reader.size_.y = png_get_image_height(reader.png_, reader.pngInfo_);
	auto color_type = png_get_color_type(reader.png_, reader.pngInfo_);
	auto bit_depth  = png_get_bit_depth(reader.png_, reader.pngInfo_);

	// resolve palette colors
	if(color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(reader.png_);
		color_type = PNG_COLOR_TYPE_RGB;
	}

	// we can't ever handle formats with less than 8 bits
	if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
		png_set_expand_gray_1_2_4_to_8(reader.png_);
		bit_depth = 8;
	} else if(bit_depth < 8) {
		png_set_expand(reader.png_);
		bit_depth = 8;
	}

	Format format;
	constexpr auto forceRGBA8 = false;

	if(forceRGBA8) {
		if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
			png_set_gray_to_rgb(reader.png_);
			color_type = PNG_COLOR_TYPE_RGB;
		}

		if(bit_depth == 16) {
			png_set_strip_16(reader.png_);
			bit_depth = 8;
		}

		if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY) {
			png_set_filler(reader.png_, 0xFF, PNG_FILLER_AFTER);
			color_type = PNG_COLOR_TYPE_RGBA;
		}

		if(color_type != PNG_COLOR_TYPE_RGBA || bit_depth != 8) {
			dlg_error("Unsupported png format");
			return ReadError::unsupportedFormat;
		}

		format = Format::r8g8b8a8Srgb;
	} else {
		// we force alpha for rgb images. rgb has bad vulkan support
		constexpr auto forceRGBAlpha = true;
		if(forceRGBAlpha && color_type == PNG_COLOR_TYPE_RGB) {
			if(png_get_valid(reader.png_, reader.pngInfo_, PNG_INFO_tRNS)) {
				png_set_tRNS_to_alpha(reader.png_);
			}

			png_set_filler(reader.png_, 0xFF, PNG_FILLER_AFTER);
			color_type = PNG_COLOR_TYPE_RGBA;
		}

		if(bit_depth != 16 && bit_depth != 8) {
			dlg_error("Unsupported png bit depth {}", u32(bit_depth));
			return ReadError::unsupportedFormat;
		}

		switch(color_type) {
			case PNG_COLOR_TYPE_GRAY:
				format = (bit_depth == 16) ? Format::r16Unorm : Format::r8Srgb;
				break;
			case PNG_COLOR_TYPE_RGB:
				format = (bit_depth == 16) ? Format::r16g16b16Unorm : Format::r8g8b8Srgb;
				break;
			case PNG_COLOR_TYPE_RGBA:
				format = (bit_depth == 16) ? Format::r16g16b16a16Unorm : Format::r8g8b8a8Srgb;
				break;
			case PNG_COLOR_TYPE_GRAY_ALPHA:
				dlg_trace("gray|alpha png");
				format = (bit_depth == 16) ? Format::r16g16Unorm : Format::r8g8Srgb;
				break;
			default:
				dlg_error("Unsupported png color type");
				return ReadError::unsupportedFormat;
		}
	}

	png_read_update_info(reader.png_, reader.pngInfo_);
	reader.stream_ = std::move(stream);
	reader.format_ = format;
	return ReadError::none;
}

ReadError loadPng(std::unique_ptr<Read>&& stream,
		std::unique_ptr<ImageProvider>& ret) {
	auto reader = std::make_unique<PngReader>();
	auto err = loadPng(std::move(stream), *reader);
	if(err == ReadError::none) {
		ret = std::move(reader);
	}

	return err;
}

WriteError writePngThrow(Write& write, const ImageProvider& img) {
	if(img.size().z > 1) {
		dlg_warn("writeExr: discarding {} slices", img.size().z - 1);
	}

	if(img.mipLevels() > 1) {
		dlg_warn("writeExr: discarding {} mips", img.mipLevels() - 1);
	}

	if(img.layers() > 1) {
		dlg_warn("writeExr: discarding {} layers", img.layers() - 1);
	}

	auto png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
		nullptr, nullptr, nullptr);
	if(!png) {
		dlg_error("png_create_write_struct returned null");
		return WriteError::internal;
	}

	auto info = png_create_info_struct(png);
	if(!info) {
		dlg_error("png_create_info_struct returned null");
		return WriteError::internal;
	}

	if(::setjmp(png_jmpbuf(png))) {
		dlg_error("png error (jmpbuf)");
		return WriteError::internal;
	}

	auto writeFunc = [](png_structp png_ptr, png_bytep data, png_size_t length) {
		Write* writer = (Write*) png_get_io_ptr(png_ptr);
		writer->write(reinterpret_cast<std::byte*>(data), length);
	};

	auto flushFunc = [](png_structp) {};

	png_set_write_fn(png, static_cast<void*>(&write), writeFunc, flushFunc);
	auto type = 0;
	auto comps = 0;
	auto bitDepth = 0;
	auto fmt = img.format();

	if(fmt == Format::r8Unorm || fmt == Format::r8Srgb) {
		type = PNG_COLOR_TYPE_GRAY;
		bitDepth = 8;
		comps = 1;
	} else if(fmt == Format::r8g8b8Unorm || fmt == Format::r8g8b8Srgb) {
		type = PNG_COLOR_TYPE_RGB;
		bitDepth = 8;
		comps = 3;
	} else if(fmt == Format::r8g8b8a8Unorm || fmt == Format::r8g8b8a8Srgb) {
		type = PNG_COLOR_TYPE_RGBA;
		bitDepth = 8;
		comps = 4;
	} else if(fmt == Format::r16Unorm) {
		type = PNG_COLOR_TYPE_GRAY;
		bitDepth = 16;
		comps = 1;
	} else if(fmt == Format::r16g16b16Unorm) {
		type = PNG_COLOR_TYPE_GRAY;
		bitDepth = 16;
		comps = 3;
	} else if(fmt == Format::r16g16b16a16Unorm) {
		type = PNG_COLOR_TYPE_GRAY;
		bitDepth = 16;
		comps = 4;
	} else {
		dlg_error("Unsupported format for writing png");
		return WriteError::unsupportedFormat;
	}

	png_set_IHDR(png, info, img.size().x, img.size().y,
		bitDepth, type, PNG_INTERLACE_NONE,
    	PNG_COMPRESSION_TYPE_DEFAULT,
    	PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	auto s = img.size();
	auto data = img.read();
	if(data.size() != s.x * s.y * comps) {
		dlg_error("Invalid image data size. Expected {}, got {}",
			s.x * s.y * comps, data.size());
		return WriteError::readError;
	}

	auto rows = std::make_unique<png_bytep[]>(s.y);
	for(auto y = 0u; y < img.size().y; ++y) {
		auto off = y * s.x * comps;

		// ugh, the libpng api is terrible. This param should be const
		auto ptr = reinterpret_cast<const unsigned char*>(data.data() + off);
		rows[y] = const_cast<unsigned char*>(ptr);
	}

	png_write_image(png, rows.get());
	png_write_end(png, nullptr);
	png_destroy_write_struct(&png, &info);
	return WriteError::none;
}

WriteError writePng(Write& write, const ImageProvider& img) {
	try {
		return writePngThrow(write, img);
	} catch(const std::runtime_error& err) {
		dlg_error("writePng: {}", err.what());
		return WriteError::cantWrite;
	}
}

WriteError writePng(StringParam path, const ImageProvider& img) {
	auto file = FileHandle(path, "wb");
	if(!file) {
		dlg_debug("fopen: {}", std::strerror(errno));
		return WriteError::cantOpen;
	}

	FileWrite writer(std::move(file));
	return writePng(writer, img);
}

} // namespace
