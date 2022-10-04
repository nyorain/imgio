// #include <iro/config.hpp>
#define IMGIO_LINUX

#include <imgio/stream.hpp>
#include <imgio/allocation.hpp>

#include <dlg/dlg.hpp>
#include <cstdio>
#include <cerrno>

// on linux we use mmap for StreamMemoryMap
#ifdef IMGIO_LINUX
	#include <sys/mman.h>
	#include <unistd.h>
	#include <fcntl.h>
#endif

namespace imgio {
namespace {

int streamStbiRead(void *user, char *data, int size) {
	auto stream = static_cast<Stream*>(user);
	return stream->readPartial(reinterpret_cast<std::byte*>(data), size);
}

void streamStbiSkip(void *user, int n) {
	auto stream = static_cast<Stream*>(user);
	stream->seek(n, Stream::SeekOrigin::curr);
}

int streamStbiEof(void *user) {
	auto stream = static_cast<Stream*>(user);
	return stream->eof();
}

int cSeekOrigin(Stream::SeekOrigin origin) {
	switch(origin) {
		case Stream::SeekOrigin::set: return SEEK_SET;
		case Stream::SeekOrigin::curr: return SEEK_CUR;
		case Stream::SeekOrigin::end: return SEEK_END;
		default: throw std::logic_error("Invalid Stream::SeekOrigin");
	}
}

} // namespace tkn


const stbi_io_callbacks& streamStbiCallbacks() {
	static const stbi_io_callbacks impl = {
		streamStbiRead,
		streamStbiSkip,
		streamStbiEof,
	};
	return impl;
}

i64 FileStream::readPartial(std::byte* buf, u64 size) {
	return std::fread(buf, 1u, size, file_);
}

void FileStream::seek(i64 offset, SeekOrigin so) {
	auto res = std::fseek(file_, offset, cSeekOrigin(so));
	if(res != 0) {
		dlg_error("fseek: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::fseek failed");
	}
}

u64 FileStream::address() const {
	auto res = std::ftell(file_);
	if(res < 0) {
		dlg_error("ftell: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::ftell failed");
	}

	return u32(res);
}

bool FileStream::eof() const {
	return std::feof(file_);
}

// StreamMemoryMap
StreamMemoryMap::StreamMemoryMap(std::unique_ptr<Stream>&& stream,
		bool failOnCopy) {
	dlg_assert(stream.get());

	// If it's a file stream, try to memory map it.
	// NOTE: we could try to support file memory mapping on windows
	// as well. But not sure if memory mapping is even worth it
	// in our cases.
#ifdef IRO_LINUX
	auto tryMap = [&]{
		auto fstream = dynamic_cast<FileStream*>(stream.get());
		if(!fstream) {
			return false;
		}

		// this may fail for custom FILE objects.
		// It's not an error, we just fall back to the default non-mmap
		// implementation.
		auto fd = fileno(fstream->file());
		if(fd <= 0) {
			return false;
		}

		auto length = ::lseek(fd, 0, SEEK_END);
		if(length < 0) {
			dlg_error("lseek failed: {}", std::strerror(errno));
			return false;
		}
		size_ = length;
		mapSize_ = align(size_, getpagesize());

		auto data = ::mmap(NULL, mapSize_, PROT_READ, MAP_PRIVATE, fd, 0);
		if(data == MAP_FAILED || !data) {
			dlg_error("mmap failed: {}", std::strerror(errno));
			return false;
		}

		mmapped_ = true;
		data_ = static_cast<const std::byte*>(data);
		stream_ = std::move(stream);
		return true;
	};

	if(tryMap()) {
		return;
	}

	// otherwise fall back to default solution
#endif // TKN_LINUX

	if(auto mstream = dynamic_cast<MemoryStream*>(stream.get()); mstream) {
		data_ = mstream->buffer().data();
		size_ = mstream->buffer().size();
		mapSize_ = size_;
		stream_ = std::move(stream);
		return;
	}

	if(failOnCopy) {
		return;
	}

	// fall back to just reading the whole stream.
	stream->seek(0, Stream::SeekOrigin::end);
	size_ = stream->address();
	stream->seek(0);
	owned_ = std::make_unique<std::byte[]>(size_);
	stream->read(owned_.get(), size_);

	data_ = owned_.get();
	stream_ = std::move(stream);
	mapSize_ = size_;
}

StreamMemoryMap::~StreamMemoryMap() {
	release();
}

std::unique_ptr<Stream> StreamMemoryMap::release() {
#ifdef TKN_LINUX
	if(mmapped_ && data_) {
		::munmap(const_cast<std::byte*>(data_), size_);
	}
#endif // TKN_LINUX

	owned_ = {};
	data_ = {};
	mmapped_ = {};
	size_ = {};
	mapSize_ = {};

	return std::move(stream_);
}

void swap(StreamMemoryMap& a, StreamMemoryMap& b) {
	using std::swap;
	swap(a.owned_, b.owned_);
	swap(a.data_, b.data_);
	swap(a.mmapped_, b.mmapped_);
	swap(a.size_, b.size_);
	swap(a.mapSize_, b.mapSize_);
	swap(a.stream_, b.stream_);
}

} // namespace tkn

