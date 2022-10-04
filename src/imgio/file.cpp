#include <imgio/file.hpp>
#include <imgio/allocation.hpp>
#include <nytl/scope.hpp>
#include <dlg/dlg.hpp>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace imgio {

template<typename C>
C readFile(StringParam path, bool binary) {
	dlg_assert(!path.empty());
	errno = 0;

	auto *f = std::fopen(path.c_str(), binary ? "rb" : "r");
	if(!f) {
		dlg_warn("Could not open '{}' for reading: {}", path, std::strerror(errno));
		return {};
	}

	ScopeGuard fGuard([&]{ std::fclose(f); });

	auto ret = std::fseek(f, 0, SEEK_END);
	if(ret != 0) {
		dlg_error("fseek on '{}' failed: {}", path, std::strerror(errno));
		return {};
	}

	auto fsize = std::ftell(f);
	if(fsize < 0) {
		dlg_error("ftell on '{}' failed: {}", path, std::strerror(errno));
		return {};
	}

	ret = std::fseek(f, 0, SEEK_SET);
	if(ret != 0) {
		dlg_error("second fseek on '{}' failed: {}", path, std::strerror(errno));
		return {};
	}

	dlg_assertl(dlg_level_warn, fsize % sizeof(typename C::value_type) == 0);

	C buffer(ceilDivide(fsize, sizeof(typename C::value_type)), {});
	ret = std::fread(buffer.data(), 1, fsize, f);
	if(ret != fsize) {
		dlg_error("fread on '{}' failed: {}", path, std::strerror(errno));
		return {};
	}

	return buffer;
}

template std::vector<u32> readFile<std::vector<u32>>(StringParam, bool);
template std::vector<std::byte> readFile<std::vector<std::byte>>(StringParam, bool);
template std::string readFile<std::string>(StringParam, bool);

template<typename C>
C readPath(const fs::path& path, bool binary) {
	const char* cstr;
	std::string cpy;

	if constexpr(std::is_same_v<fs::path::value_type, char>) {
		// posix fast path
		cstr = (const char*) path.c_str();
	} else {
		// thanks, bill
		cpy = path.string();
		cstr = cpy.c_str();
	}

	return readFile<C>(StringParam(cstr), binary);
}

template std::vector<u32> readPath<std::vector<u32>>(const fs::path&, bool);
template std::vector<std::byte> readPath<std::vector<std::byte>>(const fs::path&, bool);
template std::string readPath<std::string>(const fs::path&, bool);

void writeFile(StringParam path, span<const std::byte> buffer, bool binary) {
	dlg_assert(!path.empty());
	errno = 0;

	auto* f = std::fopen(path.c_str(), binary ? "wb" : "w");
	if(!f) {
		dlg_error("Could not open '{}' for writing: {}", path, std::strerror(errno));
		return;
	}

	auto ret = std::fwrite(buffer.data(), 1, buffer.size(), f);
	if(ret != buffer.size()) {
		dlg_error("fwrite on '{}' failed: {}", path, std::strerror(errno));
	}

	std::fclose(f);
}

void writePath(const fs::path& path, span<const std::byte> buffer, bool binary) {
	const char* cstr;
	std::string cpy;

	if constexpr(std::is_same_v<fs::path::value_type, char>) {
		// posix fast path
		cstr = (const char*) path.c_str();
	} else {
		// thanks, bill
		cpy = path.string();
		cstr = cpy.c_str();
	}

	writeFile(StringParam(cstr), buffer, binary);
}

} // namespace imgio

