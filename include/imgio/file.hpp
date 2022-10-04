#pragma once

#include <imgio/fwd.hpp>
#include <nytl/span.hpp>
#include <nytl/stringParam.hpp>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdio>
#include <filesystem>

namespace imgio {

namespace fs = std::filesystem;

/// Reads the file at the given filepath and returns a raw buffer with its contents.
/// binary: Specifies whether the file should be read in binary mode.
/// Does not throw on error, just returns empty array and outputs error.
/// Functions defined for T:
/// - std::vector<std::byte>
/// - std::vector<u32>
/// - std::string
template<typename C>
C readFile(StringParam path, bool binary = true);

template<typename C>
C readPath(const fs::path& path, bool binary = true);

/// Writes the given buffer into the file at the given path.
/// binary: Specifies whether the file should be written in binary mode.
/// Does not throw on error, just outputs error.
void writeFile(StringParam path, span<const std::byte> buffer, bool binary = true);
void writePath(const fs::path& path, span<const std::byte> buffer, bool binary = true);

struct FileDeleter {
	void operator()(std::FILE* file) {
		if(file) {
			std::fclose(file);
		}
	}
};

// RAII std::FILE* handle.
class FileHandle : public std::unique_ptr<std::FILE, FileDeleter> {
public:
	using Base = std::unique_ptr<std::FILE, FileDeleter>;
	using Base::unique_ptr;

	FileHandle() = default;
	FileHandle(StringParam path, StringParam mode) :
		Base(std::fopen(path.c_str(), mode.c_str())) {}

	operator std::FILE*() const { return get(); }
	operator bool() const { return get(); }
};

inline FileHandle openFile(const char* path, const char* mode) {
	auto handle = std::fopen(path, mode);
	return FileHandle{handle};
}

} // namespace
