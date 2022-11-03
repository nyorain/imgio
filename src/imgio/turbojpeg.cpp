#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/format.hpp>
#include <nytl/span.hpp>
#include <nytl/scope.hpp>
#include <dlg/dlg.hpp>
#include <turbojpeg.h>

#include <cstdio>
#include <vector>

namespace imgio {

class JpegReader : public ImageProvider {
public:
	Vec2ui size_;
	tjhandle jpeg_ {};
	ReadStreamMemoryMap mmap_;
	mutable std::vector<std::byte> tmpData_;

public:
	~JpegReader() {
		if(jpeg_) {
			::tjDestroy(jpeg_);
		}
	}

	Format format() const noexcept override { return Format::r8g8b8a8Srgb; }
	Vec3ui size() const noexcept override { return {size_.x, size_.y, 1u}; }

	u64 read(span<std::byte> data, unsigned mip,
			unsigned layer) const override {
		dlg_assert(mip == 0);
		dlg_assert(layer == 0);
		dlg_assert(data.data());

		auto byteSize = size_.x * size_.y * formatElementSize(format());
		dlg_assert(data.size() >= byteSize);

		auto src = reinterpret_cast<const unsigned char*>(mmap_.data());
		auto dst = reinterpret_cast<unsigned char*>(data.data());
		auto res = ::tjDecompress2(jpeg_, src, mmap_.size(),
			dst, size_.x, 0, size_.y, TJPF_RGBA, TJFLAG_FASTDCT);
		if(res != 0u) {
			auto err = tjGetErrorStr2(jpeg_);
			auto msg = dlg::format("tjDecompress2: {} ({})", err, res);
			dlg_warn(msg);
			throw std::runtime_error(msg);
		}

		return byteSize;
	}

	span<const std::byte> read(unsigned mip, unsigned layer) const override {
		tmpData_.resize(size_.x * size_.y * formatElementSize(format()));
		auto res = read(tmpData_, mip, layer);
		dlg_assert(res == tmpData_.size());
		return tmpData_;
	}
};

ReadError loadJpeg(std::unique_ptr<Read>&& stream, JpegReader& reader) {
	reader.mmap_ = ReadStreamMemoryMap(std::move(stream));

	// Somewhat hacky: when reading fails, we don't take ownership of stream.
	// But StreamMemoryMap already took the stream. When we return unsuccesfully,
	// we have to return ownership. On success (see the end of the function),
	// we simply unset this guard.
	auto returnGuard = ScopeGuard([&]{
		stream = reader.mmap_.release();
	});

	reader.jpeg_ = ::tjInitDecompress();
	if(!reader.jpeg_) {
		dlg_warn("Can't initialize jpeg decompressor");
		return ReadError::internal;
	}

	int width, height;

	auto data = reinterpret_cast<const unsigned char*>(reader.mmap_.data());
	int subsamp, colorspace;
	int res = ::tjDecompressHeader3(reader.jpeg_, data, reader.mmap_.size(),
		&width, &height, &subsamp, &colorspace);
	if(res) {
		// in this case, it's propbably just no jpeg
		return ReadError::invalidType;
	}

	reader.size_.x = width;
	reader.size_.y = height;
	returnGuard.unset();
	return ReadError::none;
}

ReadError loadJpeg(std::unique_ptr<Read>&& stream, std::unique_ptr<ImageProvider>& ret) {
	auto reader = std::make_unique<JpegReader>();
	auto err = loadJpeg(std::move(stream), *reader);
	if(err == ReadError::none) {
		ret = std::move(reader);
	}

	return err;
}

} // namespace
