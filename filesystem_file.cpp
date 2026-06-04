#include "vfs.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace vfs {

bool FileSystem::removeNode(const std::string& path) {
    if (!session_.loggedIn) {
        return false;
    }

    std::string parentPath;
    std::string leaf;
    if (!splitParent(path, parentPath, leaf)) {
        std::cout << "路径不合法。\n";
        return false;
    }

    const std::uint32_t parentInodeNo = resolvePath(parentPath);
    if (parentInodeNo == kInvalidInode) {
        std::cout << "父目录不存在。\n";
        return false;
    }

    std::vector<DirEntry> entries;
    if (!readDirectoryEntries(parentInodeNo, entries)) {
        std::cout << "父目录读取失败。\n";
        return false;
    }

    auto it = std::find_if(entries.begin(), entries.end(), [&](const DirEntry& entry) {
        return readName(entry.name, kNameSize) == leaf;
    });
    if (it == entries.end()) {
        std::cout << "目标不存在。\n";
        return false;
    }
    if (it->inode == kRootInode) {
        std::cout << "不能删除根目录。\n";
        return false;
    }

    DiskInode inode = readInode(it->inode);
    if (!accessAllowed(inode, true, true)) {
        std::cout << "没有删除权限。\n";
        return false;
    }
    for (const auto& open : openFiles_) {
        if (open.used && open.inodeNo == it->inode) {
            std::cout << "文件仍处于打开状态，无法删除。\n";
            return false;
        }
    }
    if (inode.type == static_cast<std::uint8_t>(InodeType::Directory)) {
        std::vector<DirEntry> childEntries;
        readDirectoryEntries(it->inode, childEntries);
        if (childEntries.size() > 2) {
            std::cout << "目录非空，无法删除。\n";
            return false;
        }
    }

    for (std::uint32_t& blockNo : inode.direct) {
        if (blockNo != 0) {
            bfreeInternal(blockNo);
            blockNo = 0;
        }
    }

    const std::uint32_t targetInode = it->inode;
    inode = {};
    writeInode(targetInode, inode);
    ifree(targetInode);
    entries.erase(it);
    writeDirectoryEntries(parentInodeNo, entries);
    flushSuper();
    disk_.sync();
    std::cout << "删除成功。\n";
    return true;
}

int FileSystem::allocateSystemOpen(OpenMode mode, std::uint32_t inodeNo, std::uint32_t offset) {
    for (std::size_t i = 0; i < openFiles_.size(); ++i) {
        if (!openFiles_[i].used) {
            openFiles_[i].used = true;
            openFiles_[i].mode = mode;
            openFiles_[i].inodeNo = inodeNo;
            openFiles_[i].offset = offset;
            openFiles_[i].refCount = 1;
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FileSystem::allocateUserFd(int sysIndex) {
    for (std::size_t i = 0; i < session_.openFiles.size(); ++i) {
        if (session_.openFiles[i] == -1) {
            session_.openFiles[i] = sysIndex;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool FileSystem::closeFd(int fd, bool verbose) {
    if (fd < 0 || fd >= static_cast<int>(session_.openFiles.size()) || session_.openFiles[fd] == -1) {
        if (verbose) {
            std::cout << "fd 无效。\n";
        }
        return false;
    }

    const int sysIndex = session_.openFiles[fd];
    session_.openFiles[fd] = -1;
    if (sysIndex >= 0 && sysIndex < static_cast<int>(openFiles_.size()) && openFiles_[sysIndex].used) {
        openFiles_[sysIndex] = OpenFile{};
    }
    if (verbose) {
        std::cout << "文件已关闭。\n";
    }
    disk_.sync();
    return true;
}

bool FileSystem::readFileContent(std::uint32_t inodeNo, std::vector<char>& content) const {
    const DiskInode inode = readInode(inodeNo);
    if (!inode.used || inode.type != static_cast<std::uint8_t>(InodeType::File)) {
        return false;
    }

    content.assign(inode.size, 0);
    std::uint32_t copied = 0;
    for (std::uint32_t blockNo : inode.direct) {
        if (blockNo == 0 || copied >= inode.size) {
            continue;
        }

        const std::uint32_t chunk = std::min(kBlockSize, inode.size - copied);
        std::memcpy(content.data() + copied, disk_.blockPtr(blockNo), chunk);
        copied += chunk;
    }
    return true;
}

bool FileSystem::writeFileContent(std::uint32_t inodeNo, const std::vector<char>& content) {
    if (content.size() > kMaxFileSize) {
        std::cout << "文件过大，当前设计最多支持 " << kMaxFileSize << " 字节。\n";
        return false;
    }

    DiskInode inode = readInode(inodeNo);
    const std::uint32_t blocksNeeded =
        content.empty() ? 0 : (static_cast<std::uint32_t>(content.size()) + kBlockSize - 1) / kBlockSize;
    std::uint32_t currentBlocks = 0;
    for (std::uint32_t blockNo : inode.direct) {
        if (blockNo != 0) {
            ++currentBlocks;
        }
    }

    while (currentBlocks < blocksNeeded) {
        const int newBlock = ballocInternal();
        if (newBlock < 0) {
            std::cout << "磁盘空间不足。\n";
            return false;
        }
        inode.direct[currentBlocks++] = static_cast<std::uint32_t>(newBlock);
    }
    while (currentBlocks > blocksNeeded) {
        const std::uint32_t blockNo = inode.direct[currentBlocks - 1];
        inode.direct[currentBlocks - 1] = 0;
        bfreeInternal(blockNo);
        --currentBlocks;
    }

    std::vector<char> padded(blocksNeeded * kBlockSize, 0);
    if (!content.empty()) {
        std::memcpy(padded.data(), content.data(), content.size());
    }
    for (std::uint32_t i = 0; i < blocksNeeded; ++i) {
        disk_.writeBytes(inode.direct[i] * kBlockSize, padded.data() + i * kBlockSize, kBlockSize);
    }

    inode.size = static_cast<std::uint32_t>(content.size());
    writeInode(inodeNo, inode);
    flushSuper();
    disk_.sync();
    return true;
}

}  // namespace vfs
