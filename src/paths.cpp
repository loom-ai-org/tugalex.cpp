#include <cstdlib>
#include <mutex>

#include "tugalex/tugalex.hpp"

#ifndef TUGALEX_DEFAULT_DATA_DIR
#error "TUGALEX_DEFAULT_DATA_DIR must name the installed data directory"
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
}  // namespace

std::string data_dir() {
    {
        std::lock_guard<std::mutex> lock(override_mutex());
        if (!override_dir().empty()) return override_dir();
    }
    if (const char* env = std::getenv("TUGALEX_DATA_DIR"); env != nullptr && *env != '\0') return env;
    return TUGALEX_DEFAULT_DATA_DIR;
}

void set_data_dir(std::string path) {
    std::lock_guard<std::mutex> lock(override_mutex());
    override_dir() = std::move(path);
}

std::string noun_gender_path() { return data_dir() + "/noun_gender_exceptions.csv.gz"; }
std::string given_names_path() { return data_dir() + "/given_names.csv.gz"; }

}  // namespace tugalex
