#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <dlg/dlg.hpp>
#include <nytl/scope.hpp>
#include <vector>

#include <webp/decode.h>

namespace imgio {

class WebpReader : public ImageProvider {
public:
	ReadStreamMemoryMap mmap_;
	u32 width_ {};
	u32 height_ {};
	mutable std::vector<std::byte> tmpData_;

public:
	Vec3ui size() const noexcept override { return {width_, height_, 1u}; }
	Format format() const noexcept override { return Format::r8g8b8a8Srgb; }
	unsigned mipLevels() const noexcept override { return 1u; }
	// TODO: parse animations, returns them as layers
	unsigned layers() const noexcept override { return 1u; }
	bool cubemap() const noexcept override { return false; }

	nytl::Span<const std::byte> read(unsigned mip, unsigned layer) const override {
		dlg_assert(mip == 0u);
		dlg_assert(layer == 0u);
		tmpData_.resize(width_ * height_ * 4);
		read(tmpData_, mip, layer);
		return tmpData_;
	}

	u64 read(nytl::Span<std::byte> data, unsigned mip, unsigned layer) const override {
		(void) mip;
		(void) layer;
		dlg_assert(data.size() >= 4 * width_ * height_);
		auto res = WebPDecodeRGBAInto((const u8*) mmap_.data(), mmap_.mapSize(),
			(u8*) data.data(), data.size(), 4 * width_);
		dlg_assert(res);
		return 4 * width_ * height_;
	}
};

ReadError loadWebp(std::unique_ptr<Read>&& stream, WebpReader& reader) {
	reader.mmap_ = ReadStreamMemoryMap(std::move(stream));

	// Somewhat hacky: when reading fails, we don't take ownership of stream.
	// But StreamMemoryMap already took the stream. When we return unsuccesfully,
	// we have to return ownership. On success (see the end of the function),
	// we simply unset this guard.
	auto returnGuard = nytl::ScopeGuard([&]{
		stream = reader.mmap_.release();
	});

	auto data = reader.mmap_.span();
	int w, h;
	auto res = WebPGetInfo(reinterpret_cast<const u8*>(data.data()), data.size(), &w, &h);
	if(!res) {
		return ReadError::invalidType;
	}

	reader.width_ = w;
	reader.height_ = h;
	returnGuard.unset();

	return ReadError::none;
}

ReadError loadWebp(std::unique_ptr<Read>&& stream,
		std::unique_ptr<ImageProvider>& ret) {
	auto reader = std::make_unique<WebpReader>();
	auto res = loadWebp(std::move(stream), *reader);
	if(res == ReadError::none) {
		ret = std::move(reader);
	}

	return res;
}

} // namespace

