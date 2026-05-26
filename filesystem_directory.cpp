#include "vfs.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace vfs {

std::vector<DirEntry> FileSystem::makeInitialDirectory(std::uint32_t self, std::uint32_t parent) {
    std::vector<DirEntry> entries;
    entries.push_back(makeDirEntry(".", self, InodeType::Directory));
    entries.push_back(makeDirEntry("..", parent, InodeType::Directory));
    return entries;
}

DirEntry FileSystem::makeDirEntry(const std::string& name, std::uint32_t inodeNo, InodeType type) {
    DirEntry entry{};
    entry.inode = inodeNo;
    entry.type = static_cast<std::uint8_t>(type);
    copyName(entry.name, name);
    return entry;
}

bool FileSystem::readDirectoryEntries(std::uint32_t inodeNo, std::vector<DirEntry>& entries) const {
    const DiskInode inode = readInode(inodeNo);
    if (!inode.used || inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        return false;
    }

    entries.clear();
    const std::uint32_t total = inode.size / sizeof(DirEntry);
    std::uint32_t loaded = 0;

    for (std::uint32_t blockNo : inode.direct) {
        if (blockNo == 0 || loaded >= total) {
            continue;
        }

        const char* block = disk_.blockPtr(blockNo);
        for (std::uint32_t i = 0; i < kBlockSize / sizeof(DirEntry) && loaded < total; ++i, ++loaded) {
            DirEntry entry{};
            std::memcpy(&entry, block + i * sizeof(DirEntry), sizeof(DirEntry));
            if (!readName(entry.name, kNameSize).empty()) {
                entries.push_back(entry);
            }
        }
    }

    return true;
}

bool FileSystem::writeDirectoryEntries(std::uint32_t inodeNo, const std::vector<DirEntry>& entries) {
    DiskInode inode = readInode(inodeNo);
    if (!inode.used || inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        return false;
    }

    const std::uint32_t bytesNeeded = static_cast<std::uint32_t>(entries.size() * sizeof(DirEntry));
    const std::uint32_t blocksNeeded = std::max(1u, (bytesNeeded + kBlockSize - 1) / kBlockSize);
    std::uint32_t currentBlocks = 0;

    for (std::uint32_t blockNo : inode.direct) {
        if (blockNo != 0) {
            ++currentBlocks;
        }
    }
    if (blocksNeeded > kNAddr) {
        return false;
    }

    while (currentBlocks < blocksNeeded) {
        const int block = ballocInternal();
        if (block < 0) {
            return false;
        }
        inode.direct[currentBlocks++] = static_cast<std::uint32_t>(block);
    }
    while (currentBlocks > blocksNeeded) {
        const std::uint32_t blockNo = inode.direct[currentBlocks - 1];
        inode.direct[currentBlocks - 1] = 0;
        bfreeInternal(blockNo);
        --currentBlocks;
    }

    std::vector<char> buffer(blocksNeeded * kBlockSize, 0);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::memcpy(buffer.data() + i * sizeof(DirEntry), &entries[i], sizeof(DirEntry));
    }
    for (std::uint32_t i = 0; i < blocksNeeded; ++i) {
        disk_.writeBytes(inode.direct[i] * kBlockSize, buffer.data() + i * kBlockSize, kBlockSize);
    }

    inode.size = static_cast<std::uint32_t>(entries.size() * sizeof(DirEntry));
    writeInode(inodeNo, inode);
    flushSuper();
    return true;
}

bool FileSystem::findEntry(std::uint32_t dirInode, const std::string& name, DirEntry& foundEntry) const {
    std::vector<DirEntry> entries;
    if (!readDirectoryEntries(dirInode, entries)) {
        return false;
    }

    for (const auto& entry : entries) {
        if (readName(entry.name, kNameSize) == name) {
            foundEntry = entry;
            return true;
        }
    }

    return false;
}

std::uint32_t FileSystem::resolvePath(const std::string& path) const {
    const std::string clean = trim(path);
    if (clean.empty()) {
        return session_.cwdInode;
    }
    if (clean == "/") {
        return kRootInode;
    }

    std::uint32_t current = clean[0] == '/' ? kRootInode : session_.cwdInode;
    std::stringstream ss(clean);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (token.empty() || token == ".") {
            continue;
        }

        DirEntry found{};
        if (token == "..") {
            if (!findEntry(current, "..", found)) {
                return kInvalidInode;
            }
        } else if (!findEntry(current, token, found)) {
            return kInvalidInode;
        }
        current = found.inode;
    }

    return current;
}

bool FileSystem::createNode(const std::string& path, InodeType type) {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return false;
    }

    std::string parentPath;
    std::string leaf;
    if (!splitParent(path, parentPath, leaf)) {
        std::cout << "路径不合法。\n";
        return false;
    }
    if (leaf == "." || leaf == ".." || leaf.size() >= kNameSize) {
        std::cout << "名称不合法或过长。\n";
        return false;
    }

    const std::uint32_t parentInodeNo = resolvePath(parentPath);
    if (parentInodeNo == kInvalidInode) {
        std::cout << "父目录不存在。\n";
        return false;
    }

    const DiskInode parent = readInode(parentInodeNo);
    if (parent.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        std::cout << "父路径不是目录。\n";
        return false;
    }
    if (!accessAllowed(parent, true, true)) {
        std::cout << "没有在父目录创建项目的权限。\n";
        return false;
    }

    DirEntry existing{};
    if (findEntry(parentInodeNo, leaf, existing)) {
        std::cout << "同名文件或目录已经存在。\n";
        return false;
    }

    const int inodeNo = ialloc();
    if (inodeNo < 0) {
        std::cout << "没有空闲 inode 可用。\n";
        return false;
    }

    DiskInode inode{};
    inode.used = 1;
    inode.type = static_cast<std::uint8_t>(type);
    inode.uid = session_.uid;
    inode.permissions = type == InodeType::Directory ? 0700 : 0600;
    inode.linkCount = 1;
    inode.size = 0;
    writeInode(static_cast<std::uint32_t>(inodeNo), inode);

    if (type == InodeType::Directory) {
        const int block = ballocInternal();
        if (block < 0) {
            inode.used = 0;
            writeInode(static_cast<std::uint32_t>(inodeNo), inode);
            ifree(static_cast<std::uint32_t>(inodeNo));
            std::cout << "没有空闲数据块可用。\n";
            return false;
        }

        inode.direct[0] = static_cast<std::uint32_t>(block);
        writeInode(static_cast<std::uint32_t>(inodeNo), inode);
        if (!writeDirectoryEntries(static_cast<std::uint32_t>(inodeNo),
                                   makeInitialDirectory(static_cast<std::uint32_t>(inodeNo), parentInodeNo))) {
            std::cout << "目录初始化失败。\n";
            return false;
        }
    }

    std::vector<DirEntry> entries;
    readDirectoryEntries(parentInodeNo, entries);
    entries.push_back(makeDirEntry(leaf, static_cast<std::uint32_t>(inodeNo), type));
    if (!writeDirectoryEntries(parentInodeNo, entries)) {
        std::cout << "父目录已满或写入失败。\n";
        return false;
    }

    flushSuper();
    disk_.sync();
    std::cout << (type == InodeType::Directory ? "目录" : "文件") << "创建成功。\n";
    return true;
}

void FileSystem::makeDirectory() {
    createNode(readLine("请输入待创建目录路径: "), InodeType::Directory);
}

void FileSystem::changeDirectory() {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return;
    }

    const std::uint32_t inodeNo = resolvePath(readLine("请输入目标目录路径: "));
    if (inodeNo == kInvalidInode) {
        std::cout << "路径不存在。\n";
        return;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        std::cout << "目标不是目录。\n";
        return;
    }
    if (!accessAllowed(inode, true, false)) {
        std::cout << "没有进入该目录的权限。\n";
        return;
    }

    session_.cwdInode = inodeNo;
    std::cout << "当前目录已切换到 " << currentPath() << "。\n";
}

void FileSystem::listDirectory() {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return;
    }

    const std::string path = readLine("请输入目录路径(直接回车表示当前目录): ");
    const std::uint32_t inodeNo = resolvePath(path.empty() ? "." : path);
    if (inodeNo == kInvalidInode) {
        std::cout << "目录不存在。\n";
        return;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        std::cout << "目标不是目录。\n";
        return;
    }
    if (!accessAllowed(inode, true, false)) {
        std::cout << "没有读取目录的权限。\n";
        return;
    }

    std::vector<DirEntry> entries;
    readDirectoryEntries(inodeNo, entries);
    std::cout << std::left << std::setw(10) << "类型" << std::setw(18) << "名称"
              << std::setw(8) << "inode" << std::setw(8) << "大小" << "权限\n";
    for (const auto& entry : entries) {
        const DiskInode child = readInode(entry.inode);
        std::cout << std::left << std::setw(10)
                  << (child.type == static_cast<std::uint8_t>(InodeType::Directory) ? "DIR" : "FILE")
                  << std::setw(18) << readName(entry.name, kNameSize) << std::setw(8) << entry.inode
                  << std::setw(8) << child.size << std::oct << child.permissions << std::dec << '\n';
    }
}

std::string FileSystem::currentPath() const {
    if (!session_.loggedIn || session_.cwdInode == kRootInode) {
        return "/";
    }

    std::vector<std::string> parts;
    std::uint32_t current = session_.cwdInode;
    while (current != kRootInode) {
        DirEntry parent{};
        if (!findEntry(current, "..", parent)) {
            break;
        }
        const std::string name = findNameInParent(parent.inode, current);
        if (name.empty()) {
            break;
        }
        parts.push_back(name);
        current = parent.inode;
    }

    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        path += "/" + *it;
    }
    return path.empty() ? "/" : path;
}

std::string FileSystem::findNameInParent(std::uint32_t parentInode, std::uint32_t childInode) const {
    std::vector<DirEntry> entries;
    if (!readDirectoryEntries(parentInode, entries)) {
        return {};
    }

    for (const auto& entry : entries) {
        const std::string name = readName(entry.name, kNameSize);
        if (entry.inode == childInode && name != "." && name != "..") {
            return name;
        }
    }
    return {};
}

}  // namespace vfs
