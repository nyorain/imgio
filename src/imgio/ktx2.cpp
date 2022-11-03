#include <imgio/image.hpp>
#include <imgio/stream.hpp>
#include <imgio/allocation.hpp>
#include <imgio/format.hpp>
#include <dlg/dlg.hpp>
#include <memory>
#include "../format_utils.h"

// https://zlib.net/zlib_how.html
#include <zlib.h>

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
	u64 initialOffset_;
	bool zlib_ {};
	mutable std::unique_ptr<Read> stream_;
	mutable std::vector<std::byte> tmpData_;

	// when zlib-compressed
	mutable std::vector<std::vector<std::byte>> decodedLevels_;

public:
	// Returns the size for a single layer/face in the given mip level
	u64 faceSize(unsigned mip) const {
		return sizeBytes(size_, mip, format_);
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
		if(zlib_) {
			if(decodedLevels_[mip].empty()) {
				auto address = this->offset(mip, layer);
				stream_->seek(address);

				auto& declvl = decodedLevels_[mip];
				declvl.resize(levels_[mip].uncompressedLength);

				// TODO: could use custom, streamed, impl that does
				// not need tmpData_
				tmpData_.resize(levels_[mip].length);
				stream_->read(tmpData_.data(), tmpData_.size());

				uLongf dstLen = declvl.size();
				auto res = uncompress(reinterpret_cast<unsigned char*>(declvl.data()), &dstLen,
					reinterpret_cast<unsigned char*>(tmpData_.data()), tmpData_.size());
				dlg_assert(res == Z_OK);
				dlg_assert(dstLen == declvl.size());
			}

			auto fs = faceSize(mip);
			return nytl::bytes(decodedLevels_[mip]).subspan(layer * fs, fs);
		} else {
			tmpData_.resize(faceSize(mip));
			read(tmpData_, mip, layer);
			return tmpData_;
		}
	}

	u64 read(nytl::Span<std::byte> data, unsigned mip, unsigned layer) const override {
		if(zlib_) {
			auto buf = read(mip, layer);
			dlg_assert(buf.size() <= data.size());
			std::memcpy(data.data(), buf.data(), std::min(buf.size(), data.size()));
			return buf.size();
		} else {
			auto byteSize = this->faceSize(mip);
			dlg_assert(u64(data.size()) >= byteSize);

			auto address = this->offset(mip, layer);
			stream_->seek(address);
			stream_->read(data.data(), byteSize);
			return byteSize;
		}
	}

	u64 offset(unsigned mip = 0, unsigned layer = 0) const {
		errno = {};
		dlg_assert(mip < levels_.size());
		dlg_assert(layer < layers());

		auto& lvl = levels_[mip];
		auto byteSize = this->faceSize(mip);
		dlg_assert(lvl.uncompressedLength == byteSize * layers());

		return initialOffset_ + lvl.offset + byteSize * layer;
	}
};

constexpr std::array<u8, 12> ktx2Identifier = {
	0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

ReadError loadKtx2(std::unique_ptr<Read>&& stream, Ktx2Reader& reader) {
	reader.initialOffset_ = stream->address();

	std::array<u8, 12> identifier;
	if(!stream->readPartial(identifier)) {
		dlg_debug("KTX2 can't read identifier");
		return ReadError::unexpectedEnd;
	}

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

	if(header.supercompression == 3) {
		reader.zlib_ = true;
		reader.decodedLevels_.resize(header.levelCount);
	} else if(header.supercompression) {
		dlg_warn("KTX2 supercompression scheme {} unsupported", header.supercompression);
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

ReadError loadKtx2(std::unique_ptr<Read>&& stream,
		std::unique_ptr<ImageProvider>& ret) {
	auto reader = std::make_unique<Ktx2Reader>();
	auto res = loadKtx2(std::move(stream), *reader);
	if(res == ReadError::none) {
		ret = std::move(reader);
	}

	return res;
}

u32 typeSize(Format fmt) {
	auto vkfmt = VkFormat(fmt);
	if(FormatIsCompressed(vkfmt) || fmt == Format::undefined) {
		return 1u;
	}

	if(FormatIsPacked(vkfmt)) {
		return formatElementSize(fmt);
	}

	// TODO: does not always work, think of depth-stencil formats.
	return formatElementSize(fmt) / FormatComponentCount(vkfmt);
}

WriteError writeKtx2Throw(Write& write, const ImageProvider& img, bool useZlib) {
	auto initialAddr = write.address();
	write.write(ktx2Identifier);

	// TODO:
	// - check prohitibited formats

	auto size = img.size();
	auto format = img.format();
	auto numMips = img.mipLevels();
	auto numLayers = img.layers();
	auto fmtSize = formatElementSize(format);
	auto numFaces = 1u;
	if(img.cubemap()) {
		dlg_assert(numLayers % 6u == 0);
		numFaces = 6u;
		numLayers = numLayers / 6u;
	}

	Ktx2Header header {};
	header.vkFormat = u32(img.format());
	header.faceCount = numFaces;
	header.levelCount = numMips;
	header.supercompression = 0u;
	if(useZlib) {
		header.supercompression = 3u;
	}

	header.layerCount = numLayers > 1 ? numLayers : 0;
	header.typeSize = typeSize(img.format());

	header.pixelWidth = size.x;
	header.pixelHeight = size.y > 1 ? size.y : 0;
	header.pixelDepth = size.z > 1 ? size.z : 0;

	// TODO: we probably *have* to do the dfd thing per spec.
	header.dfdByteOffset = 0u;
	header.dfdByteLength = 0u;
	header.sgdByteOffset = 0u;
	header.sgdByteLength = 0u;
	header.kvdByteLength = 0u;
	header.kvdByteOffset = 0u;

	write.write(header);

	// level index
	auto mipIndexStart =
		sizeof(ktx2Identifier) +
		sizeof(Ktx2Header);
	auto dataStart = mipIndexStart + sizeof(Ktx2LevelInfo) * numMips;

	// NOTE: for compressed writes, this will be patched later
	auto off = dataStart;
	for(auto m = 0u; m < numMips; ++m) {
		Ktx2LevelInfo info {};
		info.offset = off;
		auto faceSize = sizeBytes(size, m, format);
		info.uncompressedLength = faceSize * numLayers * numFaces;
		info.length = info.uncompressedLength;
		off += info.length;

		write.write(info);
	}

	// write data
	off = dataStart;
	for(auto m = 0u; m < numMips; ++m) {
		auto faceSize = sizeBytes(size, m, format);

		// padding, align to 4
		auto alignment = align(fmtSize, 4u);
		u32 padding = align(off, alignment) - off;
		if(padding > 0) {
			for(auto i = 0u; i < padding; ++i) {
				write.write(std::byte{});
			}
			off += padding;
		}

		if(useZlib) {
			// TODO: don't use stack, ThreadMemScope-like instead
			constexpr auto bufSize = 32 * 1024;
			constexpr auto level = 6u;
			unsigned char buf[bufSize];

			z_stream strm {};
			auto res = deflateInit(&strm, level);
			dlg_assert(res == Z_OK);

			// track and back-patch the compressed size
			auto mipLength = u32(0u);
			for(auto l = 0u; l < numLayers; ++l) {
				for(auto f = 0u; f < numFaces; ++f) {
					auto span = img.read(m, l * numFaces + f);
					if(span.size() != faceSize) {
						dlg_debug("invalid ImageProvider read size: "
							"got {}, expected {}", span.size(), faceSize);
						return WriteError::readError;
					}

					strm.next_in = const_cast<unsigned char*>(
						reinterpret_cast<const unsigned char*>(span.data()));
					strm.avail_in = span.size();

					auto last = (l == numLayers - 1 && f == numFaces - 1);
					auto flush = last ? Z_FINISH : Z_NO_FLUSH;

					do {
						strm.avail_out = bufSize;
						strm.next_out = buf;

						res = deflate(&strm, flush);
						dlg_assert(res != Z_STREAM_ERROR);
						auto have = bufSize - strm.avail_out;
						write.write(reinterpret_cast<const std::byte*>(buf), have);
						mipLength += have;
					} while(strm.avail_out == 0);
					dlg_assert(strm.avail_in == 0);
				}
			}

			dlg_assert(res == Z_STREAM_END);
			(void) deflateEnd(&strm);

			// back-patch compressed mip size
			auto savedAddr = write.address();
			auto levelInfoOff = initialAddr + mipIndexStart +
				m * sizeof(Ktx2LevelInfo);
			write.seek(levelInfoOff, Seek::Origin::set);

			Ktx2LevelInfo info {};
			info.offset = off;
			info.uncompressedLength = faceSize * numLayers * numFaces;
			info.length = mipLength;
			write.write(info);

			if(mipLength > 1024) {
				auto uncompressedLength = numLayers * numFaces * faceSize;
				dlg_trace("mip {}: zlib compression: {} KB -> {} KB",
					m, uncompressedLength / 1024u, mipLength / 1024u);
			}

			write.seek(savedAddr, Seek::Origin::set);
			off += mipLength;
		} else {
			// just write layers directly
			for(auto l = 0u; l < numLayers; ++l) {
				for(auto f = 0u; f < numFaces; ++f) {
					auto span = img.read(m, l * numFaces + f);
					if(span.size() != faceSize) {
						dlg_debug("invalid ImageProvider read size: "
							"got {}, expected {}", span.size(), faceSize);
						return WriteError::readError;
					}

					write.write(span);
					off += span.size();
				}
			}
		}
	}

	return WriteError::none;
}

WriteError writeKtx2(Write& write, const ImageProvider& img, bool useZlib) {
	try {
		return writeKtx2Throw(write, img, useZlib);
	} catch(const std::runtime_error& err) {
		dlg_error("writeKtx2: {}", err.what());
		return WriteError::cantWrite;
	}
}

WriteError writeKtx2(StringParam path, const ImageProvider& image, bool useZlib) {
	auto file = FileHandle(path, "wb");
	if(!file) {
		dlg_debug("fopen: {}", std::strerror(errno));
		return WriteError::cantOpen;
	}

	FileWrite writer(std::move(file));
	return writeKtx2(writer, image, useZlib);
}

} // namespace

