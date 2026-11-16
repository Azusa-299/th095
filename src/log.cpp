#include "log.hpp"

#include <cstdarg>
#include <cstdio>

#include <fstream>
#include <string>

namespace th95::log {
namespace {

std::ofstream g_file;

std::string format_v(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);

    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, args);
    return out;
}

} // namespace

void open(const std::filesystem::path &path)
{
    if (g_file.is_open()) {
        return;
    }
    g_file.open(path, std::ios::out | std::ios::trunc);
}

void close()
{
    if (g_file.is_open()) {
        g_file.close();
    }
}

void print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string text = format_v(fmt, args);
    va_end(args);

    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);

    if (g_file.is_open()) {
        g_file << text;
        g_file.flush();
    }
}

} // namespace th95::log
