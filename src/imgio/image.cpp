#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/file.hpp>
#include <imgio/file.hpp>
#include <nytl/scope.hpp>
#include <nytl/vecOps.hpp>
#include <dlg/dlg.hpp>
#include <cstdio>

// Make stbi std::unique_ptr<std::byte[]> compatible.
// Needed since calling delete on a pointer allocated with malloc
// is undefined behavior.
namespace {
void* stbiRealloc(void* old, std::size_t newSize) {
	delete[] (std::byte*) old;
	return (void*) (new std::byte[newSize]);
}
} // anon namespace

// We do this so that we can use buffers returned by stbi
// in std::unique_ptr<std::byte> (since that uses delete on objects,
// and using delete on stuff allocated with std::malloc is clearly UB
// and in fact risky). There are certain guarantees C++
// gives for new (alignment-wise) and casting of std::byte buffers
// that should make this well-defined behavior.
#define STBI_FREE(p) (delete[] (std::byte*) p)
#define STBI_MALLOC(size) ((void*) new std::byte[size])
#define STBI_REALLOC(p, size) (stbiRealloc((void*) p, size))
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC // needed, otherwise we mess with other usages

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace imgio {

// S1, S2 are expected to be string-like types.
template<typename C, typename CT>
inline bool hasSuffix(std::basic_string_view<C, CT> str,
		std::basic_string_view<C, CT> suffix) {
    return str.size() >= suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// case-insensitive char traits
// see https://stackoverflow.com/questions/11635
struct CharTraitsCI : public std::char_traits<char> {
    static bool eq(char c1, char c2) { return toupper(c1) == toupper(c2); }
    static bool ne(char c1, char c2) { return toupper(c1) != toupper(c2); }
    static bool lt(char c1, char c2) { return toupper(c1) <  toupper(c2); }
    static int compare(const char* s1, const char* s2, size_t n) {
        while(n-- != 0) {
            if(toupper(*s1) < toupper(*s2)) return -1;
            if(toupper(*s1) > toupper(*s2)) return 1;
            ++s1; ++s2;
        }
        return 0;
    }
    static const char* find(const char* s, int n, char a) {
        while(n-- > 0 && toupper(*s) != toupper(a)) {
            ++s;
        }
        return s;
    }
};

// case-insensitive
inline bool hasSuffixCI(std::string_view cstr, std::string_view csuffix) {
	using CIView = std::basic_string_view<char, CharTraitsCI>;
	auto str = CIView(cstr.data(), cstr.size());
	auto suffix = CIView(csuffix.data(), csuffix.size());
	return hasSuffix(str, suffix);
}

// ImageProvider api
ReadError loadStb(std::unique_ptr<Stream>&& stream,
		std::unique_ptr<ImageProvider>& provider) {
	auto img = readImageDataStb(std::move(stream));
	if(!img.data) {
		return ReadError::internal;
	}

	provider = wrap(std::move(img));
	return ReadError::none;
}

std::unique_ptr<ImageProvider> loadImage(std::unique_ptr<Stream>&& stream,
		std::string_view ext) {
	using ImageLoader = ReadError(*)(std::unique_ptr<Stream>&& stream,
		std::unique_ptr<ImageProvider>&);

	struct {
		std::array<std::string_view, 5> exts {};
		ImageLoader loader;
		bool tried {false};
	} loaders[] = {
		// {{".png"}, &loadPng},
		// {{".jpg", ".jpeg"}, &loadJpeg},
		{{".ktx"}, &loadKtx},
		{{".ktx2"}, &loadKtx2},
		// {{".exr"}, [](auto&& stream, auto& provider) {
		// 	return loadExr(std::move(stream), provider);
		// }},
		{{".hdr", ".tga", ".bmp", ".psd", ".gif"}, &loadStb},
	};

	// Try the one with matching extension
	std::unique_ptr<ImageProvider> reader;
	if(!ext.empty()) {
		for(auto& loader : loaders) {
			bool found = false;
			for(auto& lext : loader.exts) {
				if(lext.empty()) {
					break;
				}

				if(hasSuffixCI(ext, lext)) {
					found = true;
				}
			}

			if(found) {
				loader.tried = true;
				auto res = loader.loader(std::move(stream), reader);
				if(res == ReadError::none) {
					dlg_assert(reader);
					return reader;
				}

				break;
			}
		}
	}

	// Just try out all readers
	for(auto& loader : loaders) {
		// Skip the loader that was already tried
		if(loader.tried) {
			continue;
		}

		stream->seek(0, Stream::SeekOrigin::set); // reset stream
		auto res = loader.loader(std::move(stream), reader);
		if(res == ReadError::none) {
			dlg_assert(reader);
			return reader;
		}
	}

	return {};
}

std::unique_ptr<ImageProvider> loadImage(StringParam path) {
	auto file = FileHandle(path, "rb");
	if(!file) {
		dlg_debug("fopen('{}'): {}", path, std::strerror(errno));
		return {};
	}

	return loadImage(std::make_unique<FileStream>(std::move(file)), path);
}

std::unique_ptr<ImageProvider> loadImage(FileHandle&& file) {
	return loadImage(std::make_unique<FileStream>(std::move(file)));
}

std::unique_ptr<ImageProvider> loadImage(span<const std::byte> data) {
	return loadImage(std::make_unique<MemoryStream>(data));
}

// Image api
ImageData readImageDataStb(std::unique_ptr<Stream>&& stream) {
	constexpr auto channels = 4u; // TODO: make configurable
	int width, height, ch;
	std::byte* data;
	ImageData ret;

	auto& cb = streamStbiCallbacks();
	bool hdr = stbi_is_hdr_from_callbacks(&cb, stream.get());
	stream->seek(0u, Stream::SeekOrigin::set);

	if(hdr) {
		auto fd = stbi_loadf_from_callbacks(&cb, stream.get(), &width, &height, &ch, channels);
		data = reinterpret_cast<std::byte*>(fd);
		ret.format = Format::r32g32b32a32Sfloat;
	} else {
		auto cd = stbi_load_from_callbacks(&cb, stream.get(), &width, &height, &ch, channels);
		data = reinterpret_cast<std::byte*>(cd);
		ret.format = Format::r8g8b8a8Unorm;
	}

	if(!data) {
		std::string err = "STB could not load image: ";
		err += stbi_failure_reason();
		dlg_warn("{}", err);
		return ret;
	}

	ret.data.reset(data);
	ret.size.x = width;
	ret.size.y = height;
	ret.size.z = 1u;
	stream = {}; // no longer needed, close it.
	return ret;
}

ImageData readImageData(const ImageProvider& provider, unsigned mip, unsigned layer) {
	dlg_assertlm(dlg_level_debug, provider.layers() == 1,
		"readImage: discarding {} layers", provider.layers() - 1);
	dlg_assertlm(dlg_level_debug, provider.mipLevels() == 1,
		"readImage: discarding {} mip levels", provider.mipLevels() - 1);

	ImageData ret;
	ret.format = provider.format();

	auto size = provider.size();
	size.x = std::max(size.x >> mip, 1u);
	size.y = std::max(size.y >> mip, 1u);
	size.z = std::max(size.z >> mip, 1u);

	ret.size = size;
	auto byteSize = size.x * size.y * size.z * formatElementSize(ret.format);
	ret.data = std::make_unique<std::byte[]>(byteSize);
	auto res = provider.read({ret.data.get(), ret.data.get() + byteSize}, mip, layer);
	dlg_assert(res == byteSize);

	return ret;
}

ImageData readImageData(std::unique_ptr<Stream>&& stream, unsigned mip, unsigned layer) {
	auto provider = loadImage(std::move(stream));
	return provider ? readImageData(*provider, mip, layer) : ImageData {};
}

class MemImageProvider : public ImageProvider {
public:
	struct MipLayerFace {
		std::unique_ptr<std::byte[]> owned;
		const std::byte* ref;
	};

	std::vector<MipLayerFace> data_;
	bool cubemap_ {};
	unsigned layers_;
	unsigned mips_;
	Vec3ui size_;
	Format format_;

public:
	u64 faceSize(unsigned mip) const {
		auto w = std::max(size_.x >> mip, 1u);
		auto h = std::max(size_.y >> mip, 1u);
		auto d = std::max(size_.z >> mip, 1u);
		return w * h * d * formatElementSize(format_);
	}

	unsigned layers() const noexcept override { return layers_; }
	unsigned mipLevels() const noexcept override { return mips_; }
	Vec3ui size() const noexcept override { return size_; }
	Format format() const noexcept override { return format_; }
	bool cubemap() const noexcept override { return cubemap_; }

	span<const std::byte> read(unsigned mip = 0, unsigned layer = 0) const override {
		dlg_assert(mip < mipLevels() && layer < layers());
		auto id = mip * layers_ + layer;
		return {data_[id].ref, data_[id].ref + faceSize(mip)};
	}

	u64 read(span<std::byte> data, unsigned mip = 0, unsigned layer = 0) const override {
		dlg_assert(mip < mipLevels() && layer < layers());
		auto id = mip * layers_ + layer;
		auto byteSize = faceSize(mip);
		dlg_assert(u64(data.size()) >= byteSize);
		std::memcpy(data.data(), data_[id].ref, byteSize);
		return byteSize;
	}
};

std::unique_ptr<ImageProvider> wrap(ImageData&& image) {
	dlg_assert(image.size.x >= 1 && image.size.y >= 1 && image.size.z >= 1);

	auto ret = std::make_unique<MemImageProvider>();
	ret->layers_ = ret->mips_ = 1u;
	ret->format_ = image.format;
	ret->size_ = image.size;
	ret->cubemap_ = false;
	auto& data = ret->data_.emplace_back();
	data.owned = std::move(image.data);
	data.ref = data.owned.get();
	return ret;
}

std::unique_ptr<ImageProvider> wrapImage(Vec3ui size, Format format,
		span<const std::byte> span) {
	dlg_assert(size.x >= 1 && size.y >= 1 && size.z >= 1);
	dlg_assert(span.size() >= size.x * size.y * formatElementSize(format));

	auto ret = std::make_unique<MemImageProvider>();
	ret->layers_ = ret->mips_ = 1u;
	ret->format_ = format;
	ret->size_ = size;
	ret->cubemap_ = false;
	auto& data = ret->data_.emplace_back();
	data.ref = span.data();
	return ret;
}

std::unique_ptr<ImageProvider> wrapImage(Vec3ui size, Format format,
		unsigned mips, unsigned layers,
		span<std::unique_ptr<std::byte[]>> data, bool cubemap) {
	dlg_assert(size.x >= 1 && size.y >= 1 && size.z >= 1);
	dlg_assert(mips >= 1);
	dlg_assert(layers >= 1);
	dlg_assert(data.size() == mips * layers);
	dlg_assert(!cubemap || layers % 6 == 0u);

	auto ret = std::make_unique<MemImageProvider>();
	ret->mips_ = mips;
	ret->layers_ = layers;
	ret->format_ = format;
	ret->size_ = size;
	ret->cubemap_ = cubemap;
	ret->data_.reserve(data.size());
	for(auto& d : data) {
		auto& rdi = ret->data_.emplace_back();
		rdi.owned = std::move(d);
		rdi.ref = rdi.owned.get();
	}

	return ret;
}

std::unique_ptr<ImageProvider> wrapImage(Vec3ui size, Format format,
		unsigned mips, unsigned layers, std::unique_ptr<std::byte[]> data,
		bool cubemap) {
	dlg_assert(size.x >= 1 && size.y >= 1 && size.z >= 1);
	dlg_assert(mips >= 1);
	dlg_assert(layers >= 1);
	dlg_assert(!cubemap || layers % 6 == 0u);

	auto ret = std::make_unique<MemImageProvider>();
	ret->mips_ = mips;
	ret->layers_ = layers;
	ret->format_ = format;
	ret->size_ = size;
	ret->cubemap_ = cubemap;
	ret->data_.reserve(mips * layers);

	auto fmtSize = formatElementSize(format);
	for(auto m = 0u; m < mips; ++m) {
		for(auto l = 0u; l < layers; ++l) {
			auto& rdi = ret->data_.emplace_back();
			auto off = fmtSize * tightTexelNumber(
				{size.x, size.y, size.z}, layers, m, l);
			rdi.ref = data.get() + off;
		}
	}

	ret->data_[0].owned = std::move(data);
	return ret;
}

std::unique_ptr<ImageProvider> wrapImage(Vec3ui size, Format format,
		unsigned mips, unsigned layers, span<const std::byte> data,
		bool cubemap) {
	dlg_assert(size.x >= 1 && size.y >= 1 && size.z >= 1);
	dlg_assert(mips >= 1);
	dlg_assert(layers >= 1);
	dlg_assert(!cubemap || layers % 6 == 0u);

	auto ret = std::make_unique<MemImageProvider>();
	ret->mips_ = mips;
	ret->layers_ = layers;
	ret->format_ = format;
	ret->size_ = size;
	ret->cubemap_ = cubemap;
	ret->data_.reserve(mips * layers);

	auto fmtSize = formatElementSize(format);
	for(auto m = 0u; m < mips; ++m) {
		for(auto l = 0u; l < layers; ++l) {
			auto& rdi = ret->data_.emplace_back();
			auto off = fmtSize * tightTexelNumber(
				{size.x, size.y, size.z}, layers, m, l);
			rdi.ref = data.data() + off;
		}
	}

	return ret;
}

std::unique_ptr<ImageProvider> wrapImage(Vec3ui size, Format format,
		unsigned mips, unsigned layers,
		span<const std::byte* const> data, bool cubemap) {
	dlg_assert(data.size() == mips * layers);
	dlg_assert(!cubemap || layers % 6 == 0u);

	auto ret = std::make_unique<MemImageProvider>();
	ret->mips_ = mips;
	ret->layers_ = layers;
	ret->format_ = format;
	ret->size_ = size;
	ret->cubemap_ = cubemap;
	ret->data_.reserve(data.size());
	for(auto& d : data) {
		auto& rdi = ret->data_.emplace_back();
		rdi.ref = d;
	}

	return ret;
}

// Multi
class MultiImageProvider : public ImageProvider {
public:
	std::vector<std::unique_ptr<ImageProvider>> providers_;
	bool asSlices_ {};
	unsigned mips_ {};
	bool cubemap_ {};
	Vec3ui size_ {};
	Format format_ = Format::undefined;

	mutable std::vector<std::byte> read_;

public:
	Format format() const noexcept override { return format_; }
	unsigned mipLevels() const noexcept override { return mips_; }
	unsigned layers() const noexcept override { return asSlices_ ? 1u : providers_.size(); }
	Vec3ui size() const noexcept override { return size_; }
	bool cubemap() const noexcept override { return cubemap_; }

	u64 read(span<std::byte> data, unsigned mip = 0, unsigned layer = 0) const override {
		if(asSlices_) {
			dlg_assert(mip < mips_ && layer == 0u);
			dlg_assert(size_.z == providers_.size());

			auto [width, height, _] = mipSize({size_.x, size_.y, 1}, mip);
			auto sliceSize = width * height * formatElementSize(format_);
			dlg_assert(data.size() >= size_.z * sliceSize);

			u64 ret {};
			for(auto z = 0u; z < size_.z; ++z) {
				ret += providers_[z]->read(data, mip, 0);
				data = data.subspan(sliceSize);
			}

			return ret;
		} else {
			dlg_assert(mip < mips_ && layer < layers());
			return providers_[layer]->read(data, mip, 0);
		}
	}

	span<const std::byte> read(unsigned mip = 0, unsigned layer = 0) const override {
		if(asSlices_) {
			auto [width, height, _] = mipSize({size_.x, size_.y, size_.z}, mip);
			auto sliceSize = width * height * formatElementSize(format_);
			read_.resize(size_.z * sliceSize);
			this->read(read_, mip, layer);
			return read_;
		} else {
			dlg_assert(mip < mips_ && layer < layers());
			return providers_[layer]->read(mip, 0);
		}
	}
};

std::unique_ptr<ImageProvider> loadImageLayers(
		span<const char* const> paths, bool cubemap, bool asSlices) {
	auto ret = std::make_unique<MultiImageProvider>();
	auto first = true;
	ret->cubemap_ = cubemap;
	ret->asSlices_ = asSlices;

	for(auto& path : paths) {
		auto provider = loadImage(path);
		if(!provider) {
			return {};
		}

		if(first) {
			first = false;
			ret->format_ = provider->format();
			ret->size_ = provider->size();
			ret->mips_ = provider->mipLevels();

			if(ret->size_.z > 1) {
				dlg_error("LayeredImageProvider: Image has depth {}, not allowed",
					ret->size_.z);
				return {};
			}
		} else {
			// Make sure that this image has the same properties as the
			// other images
			auto isize = provider->size();
			if(isize != ret->size_) {
				dlg_error("LayeredImageProvider: Image layer has different size:"
					"\n\tFirst image had size {}"
					"\n\t'{}' has size {}", ret->size_, path, isize);
				return {};
			}

			auto iformat = provider->format();
			if(iformat != ret->format_) {
				dlg_error("LayeredImageProvider: Image layer has different format:"
					"\n\tFirst image had format {}"
					"\n\t'{}' has format {}",
					(int) ret->format_, path, (int) iformat);
				return {};
			}

			auto imips = provider->mipLevels();
			if(imips != ret->mips_) {
				dlg_error("LayeredImageProvider: Image layer has different mip count:"
					"\n\tFirst image had mip count {}"
					"\n\t'{}' has mip count {}",
					(int) ret->mips_, path, (int) imips);
				return {};
			}
		}

		// NOTE: we could just append the layers (modifying
		// our ImageProvider a bit) if it's ever needed. Should be
		// controlled via parameter
		dlg_assertlm(dlg_level_warn, provider->layers() == 1u,
			"{} layers will not be accessible", provider->layers() - 1);
		ret->providers_.push_back(std::move(provider));
	}

	if(asSlices) {
		ret->size_.z = ret->providers_.size();
	}

	return ret;
}

} // namespace
