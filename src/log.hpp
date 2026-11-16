#pragma once

#include <filesystem>

namespace th95::log {

// Mirrors the original retail routine: printf-style logger that also writes log.txt.
void open(const std::filesystem::path &path);
void close();
void print(const char *fmt, ...);

} // namespace th95::log
