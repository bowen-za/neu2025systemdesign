#include "vfs.h"

#include <cctype>
#include <clocale>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vfs {

const char kDiskFileName[] = "virtual_disk.bin";

UserSession::UserSession() {
    openFiles.fill(-1);
}

std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(start, end - start);
}

bool splitParent(const std::string& path, std::string& parent, std::string& leaf) {
    std::string clean = trim(path);
    if (clean.empty() || clean == "/") {
        return false;
    }

    while (clean.size() > 1 && clean.back() == '/') {
        clean.pop_back();
    }

    const std::size_t pos = clean.find_last_of('/');
    if (pos == std::string::npos) {
        parent = ".";
        leaf = clean;
    } else if (pos == 0) {
        parent = "/";
        leaf = clean.substr(1);
    } else {
        parent = clean.substr(0, pos);
        leaf = clean.substr(pos + 1);
    }

    return !leaf.empty();
}

void copyName(char* dest, const std::string& name) {
    std::memset(dest, 0, kNameSize);
    std::strncpy(dest, name.c_str(), kNameSize - 1);
}

std::string readName(const char* src, std::size_t len) {
    std::size_t actual = 0;
    while (actual < len && src[actual] != '\0') {
        ++actual;
    }
    return std::string(src, src + actual);
}

void configureConsoleEncoding() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, ".UTF-8");
}

}  // namespace vfs
