#pragma once

#include <imgio/fwd.hpp>
#include <imgio/file.hpp>
#include <nytl/bytes.hpp>
#include <nytl/span.hpp>
#include <type_traits>
#include <stb_image.h>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace imgio {

class Seek {
public:
	enum class Origin {
		set,
		curr,
		end,
	};

public:
	virtual ~Seek() = default;

	// Changes the current read address.
	// To be safe, the restriction from std::fseek apply.
	// They are quite complicated but the most important points:
	// - SeekOrigin::end is not supported on binary streams
	// - for text streams, offset must be zero or (for SeekOrigin::set),
	//   a value previously returned by tell.
	virtual void seek(i64 offset, Seek::Origin seek = Seek::Origin::set) = 0;

	// Returns the current absolute address in the stream.
	virtual u64 address() const = 0;
};

// Simple abstract readable stream interface.
// Mainly a way to abstract over data coming from a file or a memory
// buffer.
class Read : public Seek {
public:
	// Returns up to 'size' bytes into 'buf'.
	// When less bytes are available, reads the maximum amount available.
	// Always returns the number of bytes read, or a negative number
	// on error. Advances the current read address by the number of
	// read bytes.
	virtual i64 readPartial(std::byte* buf, u64 size) = 0;

	// Returns whether the stream is at the end.
	// Calling seek on it will clear this if appropriate.
	virtual bool eof() const = 0;

	// Utility
	// Like readPartial but throws when an error ocurrs or the
	// buffer can't be completely filled.
	virtual void read(std::byte* buf, u64 size) {
		auto res = readPartial(buf, size);
		if(res != i64(size)) {
			throw std::out_of_range("Read::read");
		}
	}

	// Overloads that read into span-based buffers..
	virtual void read(span<std::byte> buf) {
		read(buf.data(), buf.size());
	}

	virtual i64 readPartial(span<std::byte> buf) {
		return readPartial(buf.data(), buf.size());
	}

	// Reads into the given object.
	// T must be standard layout type or vector of such.
	// Throws when the objects can't be filled completely or an error ocurrs.
	template<typename T>
	void read(T& val) {
		read(nytl::bytes(val));
	}

	// Tries to read the given object.
	// T must be standard layout type or vector of such.
	// Returns whether reading the complete object suceeded.
	template<typename T>
	bool readPartial(T& val) {
		auto bytes = nytl::bytes(val);
		return readPartial(bytes) == i64(bytes.size());
	}

	// Creates a (default-constructed) object of type T and attempts to
	// fill it from stream. T must be standard layout type or vector of such.
	// Throws when the objects can't be filled completely or an error ocurrs.
	template<typename T>
	T read() {
		T ret;
		read(ret);
		return ret;
	}
};

class Write : public Seek {
public:
	// Returns the number of bytes written (or negative code for error)
	virtual i64 writePartial(const std::byte* buf, u64 size) = 0;

	virtual void write(const std::byte* buf, u64 size) {
		auto res = writePartial(buf, size);
		if(res != i64(size)) {
			throw std::runtime_error("Write::write");
		}
	}

	virtual void write(span<const std::byte> buf) {
		write(buf.data(), buf.size());
	}

	template<typename T>
	void write(const T& val) {
		write(nytl::bytes(val));
	}
};

const stbi_io_callbacks& streamStbiCallbacks();

class FileRead : public Read {
public:
	FileRead() = default;
	FileRead(FileHandle&& file) : file_(std::move(file)) {}

	i64 readPartial(std::byte* buf, u64 size) override;
	void seek(i64 offset, Seek::Origin so) override;
	u64 address() const override;
	bool eof() const override;

	std::FILE* file() const { return file_.get(); }

protected:
	FileHandle file_;
};

struct FileWrite : public Write {
public:
	FileWrite() = default;
	FileWrite(FileHandle&& file) : file_(std::move(file)) {}

	i64 writePartial(const std::byte*, u64 size) override;
	void seek(i64 offset, Seek::Origin so) override;
	u64 address() const override;

	std::FILE* file() const { return file_.get(); }

protected:
	FileHandle file_;
};

class MemoryRead : public Read {
public:
	MemoryRead() = default;
	MemoryRead(span<const std::byte> buf) : buf_(buf) {}

	inline i64 readPartial(std::byte* buf, u64 size) override {
		auto read = std::clamp(i64(buf_.size()) - i64(at_), i64(0), i64(size));
		std::memcpy(buf, buf_.data() + at_, read);
		at_ += read;
		return read;
	}

	inline void seek(i64 offset, Seek::Origin origin) override {
		switch(origin) {
			case Seek::Origin::set: at_ = offset; break;
			case Seek::Origin::curr: at_ += offset; break;
			case Seek::Origin::end: at_ = buf_.size() + offset; break;
			default: throw std::logic_error("Invalid Stream::SeekOrigin");
		}
	}

	inline u64 address() const override { return at_; }
	inline bool eof() const override { return at_ >= buf_.size(); }

	inline span<const std::byte> buffer() const { return buf_; }

protected:
	span<const std::byte> buf_;
	u64 at_ {0};
};

// Completely maps the data of a stream into memory.
// This is done as efficiently as possible: if the stream is a memory
// stream, simply returns the arleady in-memory buffer. Otherwise,
// if the stream comes a file, tries to memory map it.
class ReadStreamMemoryMap {
public:
	ReadStreamMemoryMap() = default;

	// The MemoryMap takes ownership of the stream only if no
	// exception is thrown.
	// It can be released later on using 'release'
	// - failOnCopy: When the stream can't be mapped directly into memory,
	//   will not take ownership of the stream and also not copy the data.
	explicit ReadStreamMemoryMap(std::unique_ptr<Read>&& source,
		bool failOnCopy = false);
	~ReadStreamMemoryMap();

	ReadStreamMemoryMap(ReadStreamMemoryMap&& rhs) { swap(*this, rhs); }
	ReadStreamMemoryMap& operator=(ReadStreamMemoryMap rhs) {
		swap(*this, rhs);
		return *this;
	}

	const std::byte* data() const { return data_; }
	std::size_t size() const { return size_; }
	std::size_t mapSize() const { return mapSize_; }
	nytl::span<const std::byte> span() const { return {data_, size_}; }

	const std::byte* begin() const { return data(); }
	const std::byte* end() const { return data() + size(); }

	// Destroys the memory map and returns the stream.
	std::unique_ptr<Read> release();

	friend void swap(ReadStreamMemoryMap& a, ReadStreamMemoryMap& b);

private:
	bool mmapped_ {};
	const std::byte* data_ {};
	std::size_t size_ {};
	std::size_t mapSize_ {};
	std::unique_ptr<std::byte[]> owned_;
	std::unique_ptr<Read> stream_;
};

} // namespace imgio

