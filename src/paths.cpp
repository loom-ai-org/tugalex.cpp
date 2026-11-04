#include <dlfcn.h>
#ifdef __linux__
#include <sys/auxv.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "tugalex/tugalex.hpp"

#ifndef TUGALEX_DEFAULT_DATA_DIR
#error "TUGALEX_DEFAULT_DATA_DIR must name the installed data directory"
#endif
#if !defined(TUGALEX_DATA_FROM_LIBDIR) || !defined(TUGALEX_DATA_FROM_BINDIR)
#error "TUGALEX_DATA_FROM_LIBDIR and TUGALEX_DATA_FROM_BINDIR must name the data directory relative to the install"
#endif

namespace tugalex {

namespace {
std::mutex& override_mutex() {
    static std::mutex m;
    return m;
}
std::string& override_dir() {
    static std::string dir;
    return dir;
}

// The installed data directory beside the shared library or the executable
// that holds this code, so that an install moved to another prefix finds its
// data. Empty when that directory does not hold the data.
std::string relocated_data_dir() {
    namespace fs = std::filesystem;
    Dl_info self{};
    if (dladdr(reinterpret_cast<void*>(&data_dir), &self) == 0 || self.dli_fname == nullptr) return {};
    fs::path object = self.dli_fname;
#ifdef __linux__
    // glibc names the main program by argv[0], which need not be a path.
    Dl_info program{};
    if (dladdr(reinterpret_cast<void*>(getauxval(AT_PHDR)), &program) != 0 &&
        program.dli_fbase == self.dli_fbase)
        object = "/proc/self/exe";
#endif
    std::error_code ec;
    const fs::path dir = fs::canonical(object, ec).parent_path();
    if (ec) return {};
    for (const char* relative : {TUGALEX_DATA_FROM_LIBDIR, TUGALEX_DATA_FROM_BINDIR}) {
        const fs::path candidate = (dir / relative).lexically_normal();
        if (fs::is_regular_file(candidate / "regional_dict.csv.gz", ec)) return candidate.string();
    }
    return {};
}
}  // namespace

std::string data_dir() {
    {
        std::lock_guard<std::mutex> lock(override_mutex());
        if (!override_dir().empty()) return override_dir();
    }
    if (const char* env = std::getenv("TUGALEX_DATA_DIR"); env != nullptr && *env != '\0') return env;
    static const std::string relocated = relocated_data_dir();
    return relocated.empty() ? TUGALEX_DEFAULT_DATA_DIR : relocated;
}

void set_data_dir(std::string path) {
    std::lock_guard<std::mutex> lock(override_mutex());
    override_dir() = std::move(path);
}

std::string noun_gender_path() { return data_dir() + "/noun_gender_exceptions.csv.gz"; }
std::string given_names_path() { return data_dir() + "/given_names.csv.gz"; }

}  // namespace tugalex
