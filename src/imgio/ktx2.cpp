#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/allocation.hpp>
#include <dlg/dlg.hpp>
#include <memory>

// source: https://github.khronos.org/KTX-Specification/

namespace imgio {

struct Ktx2Header {
	u32 vkFormat;
	u32 typeSize;
	u32 pixelWidth;
	u32 pixelHeight;
	u32 pixelDepth;
	u32 layerCount;
	u32 faceCount;
	u32 levelCount;
	u32 supercompression;

	// index
	u32 dfdByteOffset;
	u32 dfdByteLength;
	u32 kvdByteOffset;
	u32 kvdByteLength;
	u32 sgdByteOffset;
	u32 sgdByteLength;
};

struct Ktx2LevelInfo {
	u64 offset;
	u64 length;
	u64 uncompressedLength;
};

class Ktx2Reader : public ImageProvider {
public:
	Format format_;
	Vec3ui size_;
	u32 faces_;
	u32 layerCount_;
	std::vector<Ktx2LevelInfo> levels_;
	mutable std::unique_ptr<Stream> stream_;
	mutable std::vector<std::byte> tmpData_;
	// mutable VulkanStreamImport import_; // when importing the mapped memory

public:
	// Returns the size for a single layer/face in the given mip level
	u64 faceSize(unsigned mip) const {
		auto w = std::max(size_.x >> mip, 1u);
		auto h = std::max(size_.y >> mip, 1u);
		auto d = std::max(size_.z >> mip, 1u);
		return w * h * d * formatElementSize(format_);
	}

	Vec3ui size() const noexcept override { return size_; }
	Format format() const noexcept override { return format_; }
	unsigned mipLevels() const noexcept override { return levels_.size(); }
	unsigned layers() const noexcept override {
		return std::max(faces_ * std::max(layerCount_, 1u), 1u);
	}
	bool cubemap() const noexcept override {
		return faces_ == 6u;
	}

	nytl::Span<const std::byte> read(unsigned mip, unsigned layer) const override {
		tmpData_.resize(faceSize(mip));
		read(tmpData_, mip, layer);
		return tmpData_;
	}

	u64 read(nytl::Span<std::byte> data, unsigned mip, unsigned layer) const override {
		auto byteSize = this->faceSize(mip);
		dlg_assert(u64(data.size()) >= byteSize);

		auto address = this->offset(mip, layer);
		stream_->seek(address);
		stream_->read(data.data(), byteSize);
		return byteSize;
	}

	u64 offset(unsigned mip = 0, unsigned layer = 0) const {
		errno = {};
		dlg_assert(mip < levels_.size());
		dlg_assert(layer < layers());

		auto& lvl = levels_[mip];
		auto byteSize = this->faceSize(mip);
		dlg_assert(lvl.uncompressedLength == byteSize * layers());

		return lvl.offset + byteSize * layer;
	}
};

ReadError loadKtx2(std::unique_ptr<Stream>&& stream, Ktx2Reader& reader) {
	std::array<u8, 12> identifier;
	if(!stream->readPartial(identifier)) {
		dlg_debug("KTX2 can't read identifier");
		return ReadError::unexpectedEnd;
	}

	constexpr std::array<u8, 12> ktx2Identifier = {
		0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
	};

	if(identifier != ktx2Identifier) {
		// dlg_debug("KTX invalid identifier");
		// for(auto i = 0u; i < 12; ++i) {
		// 	dlg_debug("{}{} vs {}", std::hex,
		// 		(unsigned) identifier[i],
		// 		(unsigned) ktxIdentifier[i]);
		// }
		return ReadError::invalidType;
	}

	Ktx2Header header;
	if(!stream->readPartial(header)) {
		dlg_debug("KTX can't read header");
		return ReadError::unexpectedEnd;
	}

	// check for unsupported cases
	auto format = Format(header.vkFormat);
	if(format == Format::undefined) {
		dlg_debug("KTX2 file with VK_FORMAT_UNDEFINED");
		return ReadError::unsupportedFormat;
	}

	if(header.pixelWidth == 0) {
		dlg_warn("KTX2 pixelWidth == 0");
		return ReadError::empty;
	}

	if(header.supercompression) {
		dlg_warn("KTX2 supercompression unsupported");
		return ReadError::unsupportedFormat;
	}

	if(header.faceCount == 0) {
		dlg_warn("KTX2 faceCount == 0, assuming faceCount == 1");
		header.faceCount = 1u;
	}

	// read index
	reader.levels_.resize(header.levelCount);
	for(auto i = 0u; i < header.levelCount; ++i) {
		if(!stream->readPartial(reader.levels_[i])) {
			dlg_debug("KTX can't read level index");
			return ReadError::unexpectedEnd;
		}
	}

	// NOTE: could debug-output key/values as we do in the KTX1 loader.

	reader.layerCount_ = header.layerCount;
	reader.faces_ = header.faceCount;
	reader.format_ = format;
	reader.size_ = {header.pixelWidth,
		std::max(header.pixelHeight, 1u),
		std::max(header.pixelDepth, 1u)};
	reader.stream_ = std::move(stream);

	return ReadError::none;
}

ReadError loadKtx2(std::unique_ptr<Stream>&& stream,
		std::unique_ptr<ImageProvider>& ret) {
	auto reader = std::make_unique<Ktx2Reader>();
	auto res = loadKtx2(std::move(stream), *reader);
	if(res == ReadError::none) {
		ret = std::move(reader);
	}

	return res;
}

} // namespace

