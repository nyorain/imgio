// #include <iro/config.hpp>
#ifndef _WIN32
  #define IMGIO_LINUX
#endif // _WIN32

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
	auto stream = static_cast<Read*>(user);
	return stream->readPartial(reinterpret_cast<std::byte*>(data), size);
}

void streamStbiSkip(void *user, int n) {
	auto stream = static_cast<Read*>(user);
	stream->seek(n, Seek::Origin::curr);
}

int streamStbiEof(void *user) {
	auto stream = static_cast<Read*>(user);
	return stream->eof();
}

int cSeekOrigin(Seek::Origin origin) {
	switch(origin) {
		case Seek::Origin::set: return SEEK_SET;
		case Seek::Origin::curr: return SEEK_CUR;
		case Seek::Origin::end: return SEEK_END;
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

// FileRead
i64 FileRead::readPartial(std::byte* buf, u64 size) {
	return std::fread(buf, 1u, size, file_);
}

void FileRead::seek(i64 offset, Seek::Origin so) {
	auto res = std::fseek(file_, offset, cSeekOrigin(so));
	if(res != 0) {
		dlg_error("fseek: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::fseek failed");
	}
}

u64 FileRead::address() const {
	auto res = std::ftell(file_);
	if(res < 0) {
		dlg_error("ftell: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::ftell failed");
	}

	return u32(res);
}

bool FileRead::eof() const {
	return std::feof(file_);
}

// FileWrite
i64 FileWrite::writePartial(const std::byte* data, u64 size) {
	return std::fwrite(data, 1u, size, file_);
}

void FileWrite::seek(i64 offset, Seek::Origin so) {
	auto res = std::fseek(file_, offset, cSeekOrigin(so));
	if(res != 0) {
		dlg_error("fseek: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::fseek failed");
	}
}

u64 FileWrite::address() const {
	auto res = std::ftell(file_);
	if(res < 0) {
		dlg_error("ftell: {} ({})", res, std::strerror(errno));
		throw std::runtime_error("FileStream::ftell failed");
	}

	return u32(res);
}

// StreamMemoryMap
ReadStreamMemoryMap::ReadStreamMemoryMap(std::unique_ptr<Read>&& stream,
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

	if(auto mstream = dynamic_cast<MemoryRead*>(stream.get()); mstream) {
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
	stream->seek(0, Seek::Origin::end);
	size_ = stream->address();
	stream->seek(0);
	owned_ = std::make_unique<std::byte[]>(size_);
	stream->read(owned_.get(), size_);

	data_ = owned_.get();
	stream_ = std::move(stream);
	mapSize_ = size_;
}

ReadStreamMemoryMap::~ReadStreamMemoryMap() {
	release();
}

std::unique_ptr<Read> ReadStreamMemoryMap::release() {
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

void swap(ReadStreamMemoryMap& a, ReadStreamMemoryMap& b) {
	using std::swap;
	swap(a.owned_, b.owned_);
	swap(a.data_, b.data_);
	swap(a.mmapped_, b.mmapped_);
	swap(a.size_, b.size_);
	swap(a.mapSize_, b.mapSize_);
	swap(a.stream_, b.stream_);
}

} // namespace tkn

