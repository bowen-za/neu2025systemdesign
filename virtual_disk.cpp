#include "vfs.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace vfs {

bool VirtualDisk::exists() const {
    std::ifstream in(kDiskFileName, std::ios::binary);
    return static_cast<bool>(in);
}

bool VirtualDisk::load() {
    std::ifstream in(kDiskFileName, std::ios::binary);
    if (!in) {
        return false;
    }

    bytes_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return bytes_.size() == kTotalBlocks * kBlockSize;
}

void VirtualDisk::createEmpty() {
    bytes_.assign(kTotalBlocks * kBlockSize, 0);
}

bool VirtualDisk::sync() const {
    std::ofstream out(kDiskFileName, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    out.write(bytes_.data(), static_cast<std::streamsize>(bytes_.size()));
    return out.good();
}

void VirtualDisk::readBytes(std::uint32_t offset, void* out, std::size_t size) const {
    std::memcpy(out, bytes_.data() + offset, size);
}

void VirtualDisk::writeBytes(std::uint32_t offset, const void* data, std::size_t size) {
    std::memcpy(bytes_.data() + offset, data, size);
}

void VirtualDisk::zeroBlock(std::uint32_t blockNo) {
    std::memset(blockPtr(blockNo), 0, kBlockSize);
}

const char* VirtualDisk::blockPtr(std::uint32_t blockNo) const {
    return bytes_.data() + static_cast<std::size_t>(blockNo) * kBlockSize;
}

char* VirtualDisk::blockPtr(std::uint32_t blockNo) {
    return bytes_.data() + static_cast<std::size_t>(blockNo) * kBlockSize;
}

}  // namespace vfs
