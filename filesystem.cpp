#include "vfs.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace vfs {

bool FileSystem::initialize() {
    if (!disk_.exists()) {
        format();
        return true;
    }

    if (!disk_.load()) {
        std::cout << "发现损坏的虚拟磁盘，正在重新格式化。\n";
        format();
        return true;
    }

    disk_.readStruct(kSuperBlockNo, super_);
    if (super_.magic != kMagic) {
        std::cout << "虚拟磁盘格式无效，正在重新格式化。\n";
        format();
        return true;
    }

    loadUsers();
    openFiles_.resize(kSystemOpenMax);
    session_ = UserSession{};
    memInodes_.clear();
    return true;
}

bool FileSystem::format() {
    disk_.createEmpty();
    initializeUsers();

    std::memset(&super_, 0, sizeof(super_));
    super_.magic = kMagic;
    super_.totalBlocks = kTotalBlocks;
    super_.inodeStartBlock = kInodeStartBlock;
    super_.inodeBlocks = kInodeBlocks;
    super_.dataStartBlock = kDataStartBlock;
    super_.dataBlocks = kDataBlocks;
    super_.inodeCount = (kInodeBlocks * kBlockSize) / sizeof(DiskInode);
    super_.rootInode = kRootInode;
    super_.userCount = kUserCount;
    super_.nextInodeHint = 1;
    super_.freeBlockCount = 1;
    super_.freeBlockStack[0] = 0;
    super_.freeInodeCount = 0;
    super_.dirty = 1;

    writeUsers();
    clearInodeArea();

    for (std::uint32_t block = kTotalBlocks - 1; block >= kDataStartBlock; --block) {
        bfreeInternal(block);
        if (block == kDataStartBlock) {
            break;
        }
    }

    DiskInode root = {};
    root.used = 1;
    root.type = static_cast<std::uint8_t>(InodeType::Directory);
    root.uid = 0;
    root.permissions = 0711;
    root.linkCount = 1;
    root.size = 0;
    writeInode(kRootInode, root);

    rebuildFreeInodeStack();
    const int rootBlock = ballocInternal();
    if (rootBlock < 0) {
        std::cout << "格式化失败：无法为根目录分配数据块。\n";
        return false;
    }

    root.direct[0] = static_cast<std::uint32_t>(rootBlock);
    writeInode(kRootInode, root);
    writeDirectoryEntries(kRootInode, makeInitialDirectory(kRootInode, kRootInode));
    createUserHomes();

    flushSuper();
    if (!disk_.sync()) {
        std::cout << "格式化失败：无法写入虚拟磁盘文件。\n";
        return false;
    }

    session_ = UserSession{};
    memInodes_.clear();
    openFiles_.assign(kSystemOpenMax, OpenFile{});
    std::cout << "文件系统已格式化完成。\n";
    return true;
}

void FileSystem::run() {
    bool running = true;
    while (running) {
        printMenu();
        const int choice = readInt("请选择功能: ");
        std::cout << '\n';

        switch (choice) {
        case 1:
            format();
            break;
        case 2:
            login();
            break;
        case 3:
            logout();
            break;
        case 4:
            createFile();
            break;
        case 5:
            openFile();
            break;
        case 6:
            writeFile();
            break;
        case 7:
            readFile();
            break;
        case 8:
            closeFile();
            break;
        case 9:
            deleteFile();
            break;
        case 10:
            makeDirectory();
            break;
        case 11:
            changeDirectory();
            break;
        case 12:
            listDirectory();
            break;
        case 13:
            running = false;
            shutdown();
            break;
        default:
            std::cout << "无效选项，请重新输入。\n";
            break;
        }

        std::cout << '\n';
    }
}

void FileSystem::initializeUsers() {
    users_.clear();
    users_.resize(kUserCount);

    for (std::uint32_t i = 0; i < kUserCount; ++i) {
        UserRecord record{};
        const std::string username = "usr" + std::to_string(i + 1);
        const std::string password = "pass" + std::to_string(i + 1);
        std::strncpy(record.username, username.c_str(), sizeof(record.username) - 1);
        std::strncpy(record.password, password.c_str(), sizeof(record.password) - 1);
        record.uid = static_cast<std::uint16_t>(i + 1);
        record.gid = static_cast<std::uint16_t>(i + 1);
        users_[i] = record;
    }
}

void FileSystem::loadUsers() {
    users_.resize(kUserCount);
    disk_.readBytes(kBootBlock * kBlockSize, users_.data(), sizeof(UserRecord) * kUserCount);
}

void FileSystem::writeUsers() {
    disk_.writeBytes(kBootBlock * kBlockSize, users_.data(), sizeof(UserRecord) * kUserCount);
}

void FileSystem::createUserHomes() {
    std::vector<DirEntry> rootEntries;
    readDirectoryEntries(kRootInode, rootEntries);

    for (const auto& user : users_) {
        const std::string username = readName(user.username, sizeof(user.username));
        const int inodeNo = ialloc();
        const int blockNo = ballocInternal();
        if (inodeNo < 0 || blockNo < 0) {
            continue;
        }

        DiskInode home{};
        home.used = 1;
        home.type = static_cast<std::uint8_t>(InodeType::Directory);
        home.uid = user.uid;
        home.permissions = 0700;
        home.linkCount = 1;
        home.size = 0;
        home.direct[0] = static_cast<std::uint32_t>(blockNo);
        writeInode(static_cast<std::uint32_t>(inodeNo), home);

        writeDirectoryEntries(static_cast<std::uint32_t>(inodeNo),
                              makeInitialDirectory(static_cast<std::uint32_t>(inodeNo), kRootInode));
        rootEntries.push_back(makeDirEntry(username, static_cast<std::uint32_t>(inodeNo),
                                           InodeType::Directory));
    }

    writeDirectoryEntries(kRootInode, rootEntries);
}

void FileSystem::clearInodeArea() {
    for (std::uint32_t block = kInodeStartBlock; block < kDataStartBlock; ++block) {
        disk_.zeroBlock(block);
    }
}

void FileSystem::flushSuper() {
    super_.dirty = 1;
    disk_.writeStruct(kSuperBlockNo, super_);
}

std::uint32_t FileSystem::inodeOffset(std::uint32_t inodeNo) const {
    return kInodeStartBlock * kBlockSize + inodeNo * sizeof(DiskInode);
}

DiskInode FileSystem::readInode(std::uint32_t inodeNo) const {
    DiskInode inode{};
    disk_.readBytes(inodeOffset(inodeNo), &inode, sizeof(inode));
    return inode;
}

void FileSystem::writeInode(std::uint32_t inodeNo, const DiskInode& inode) {
    disk_.writeBytes(inodeOffset(inodeNo), &inode, sizeof(inode));
}

int FileSystem::ballocInternal() {
    if (super_.freeBlockCount == 1) {
        const std::uint32_t leader = super_.freeBlockStack[0];
        if (leader == 0) {
            return -1;
        }

        GroupBlock group{};
        disk_.readStruct(leader, group);
        super_.freeBlockCount = group.count;
        for (std::uint32_t i = 0; i < kNicFree; ++i) {
            super_.freeBlockStack[i] = group.blocks[i];
        }
        flushSuper();
        disk_.zeroBlock(leader);
        return static_cast<int>(leader);
    }

    const std::uint32_t blockNo = super_.freeBlockStack[super_.freeBlockCount - 1];
    super_.freeBlockStack[super_.freeBlockCount - 1] = 0;
    --super_.freeBlockCount;
    flushSuper();
    disk_.zeroBlock(blockNo);
    return static_cast<int>(blockNo);
}

void FileSystem::bfreeInternal(std::uint32_t blockNo) {
    if (super_.freeBlockCount == kNicFree) {
        GroupBlock group{};
        group.count = super_.freeBlockCount;
        for (std::uint32_t i = 0; i < kNicFree; ++i) {
            group.blocks[i] = super_.freeBlockStack[i];
        }

        disk_.writeStruct(blockNo, group);
        super_.freeBlockCount = 1;
        super_.freeBlockStack[0] = blockNo;
        for (std::uint32_t i = 1; i < kNicFree; ++i) {
            super_.freeBlockStack[i] = 0;
        }
    } else {
        super_.freeBlockStack[super_.freeBlockCount] = blockNo;
        ++super_.freeBlockCount;
    }

    flushSuper();
}

void FileSystem::rebuildFreeInodeStack() {
    super_.freeInodeCount = 0;
    for (std::uint32_t i = 1; i < super_.inodeCount && super_.freeInodeCount < kNicInode; ++i) {
        const DiskInode inode = readInode(i);
        if (!inode.used) {
            super_.freeInodeStack[super_.freeInodeCount++] = i;
        }
    }
    super_.nextInodeHint = 1;
    flushSuper();
}

int FileSystem::ialloc() {
    if (super_.freeInodeCount > 0) {
        const std::uint32_t inodeNo = super_.freeInodeStack[super_.freeInodeCount - 1];
        super_.freeInodeStack[super_.freeInodeCount - 1] = 0;
        --super_.freeInodeCount;
        flushSuper();
        return static_cast<int>(inodeNo);
    }

    for (std::uint32_t i = super_.nextInodeHint; i < super_.inodeCount; ++i) {
        const DiskInode inode = readInode(i);
        if (!inode.used) {
            super_.nextInodeHint = i + 1;
            flushSuper();
            return static_cast<int>(i);
        }
    }

    return -1;
}

void FileSystem::ifree(std::uint32_t inodeNo) {
    if (inodeNo == kRootInode) {
        return;
    }

    if (super_.freeInodeCount < kNicInode) {
        super_.freeInodeStack[super_.freeInodeCount++] = inodeNo;
    }
    if (inodeNo < super_.nextInodeHint) {
        super_.nextInodeHint = inodeNo;
    }
    flushSuper();
}

MemInode* FileSystem::iget(std::uint32_t inodeNo) {
    auto it = memInodes_.find(inodeNo);
    if (it != memInodes_.end()) {
        it->second.refCount++;
        return &it->second;
    }

    MemInode mem{};
    mem.disk = readInode(inodeNo);
    mem.inodeNo = inodeNo;
    mem.refCount = 1;
    memInodes_[inodeNo] = mem;
    return &memInodes_[inodeNo];
}

void FileSystem::iput(MemInode* inode) {
    if (inode == nullptr) {
        return;
    }

    if (inode->refCount > 0) {
        --inode->refCount;
    }
    if (inode->dirty) {
        writeInode(inode->inodeNo, inode->disk);
        inode->dirty = false;
    }
    if (inode->refCount == 0) {
        memInodes_.erase(inode->inodeNo);
    }
}

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

bool FileSystem::accessAllowed(const DiskInode& inode, bool needRead, bool needWrite) const {
    if (!session_.loggedIn) {
        return false;
    }

    const bool owner = inode.uid == session_.uid;
    auto hasBit = [&](int ownerBit, int otherBit) {
        return owner ? (inode.permissions & ownerBit) != 0 : (inode.permissions & otherBit) != 0;
    };

    if (needRead && !hasBit(0400, 0004)) {
        return false;
    }
    if (needWrite && !hasBit(0200, 0002)) {
        return false;
    }
    return true;
}

std::uint32_t FileSystem::findHomeInode(const std::string& username) const {
    DirEntry home{};
    if (!findEntry(kRootInode, username, home)) {
        return kInvalidInode;
    }

    const DiskInode inode = readInode(home.inode);
    if (!inode.used || inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        return kInvalidInode;
    }
    return home.inode;
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

bool FileSystem::removeNode(const std::string& path) {
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

void FileSystem::ensureLoggedIn() const {
    if (!session_.loggedIn) {
        std::cout << "请先登录。\n";
    }
}

void FileSystem::printMenu() const {
    std::cout << "==== 模拟 UNIX 文件系统 ====\n";
    std::cout << "当前用户: " << (session_.loggedIn ? session_.username : "(未登录)") << '\n';
    std::cout << "当前目录: " << currentPath() << '\n';
    std::cout << "1. format      2. login       3. logout\n";
    std::cout << "4. create      5. open        6. write\n";
    std::cout << "7. read        8. close       9. delete\n";
    std::cout << "10. mkdir      11. chdir      12. dir\n";
    std::cout << "13. exit\n";
}

int FileSystem::readInt(const std::string& prompt) const {
    while (true) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line)) {
            return 13;
        }

        std::stringstream ss(line);
        int value = 0;
        if (ss >> value) {
            return value;
        }
        std::cout << "请输入数字。\n";
    }
}

std::string FileSystem::readLine(const std::string& prompt) const {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return trim(line);
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

void FileSystem::login() {
    if (session_.loggedIn) {
        std::cout << "当前已有用户登录，请先注销。\n";
        return;
    }

    const std::string username = readLine("用户名: ");
    const std::string password = readLine("密码: ");
    for (const auto& user : users_) {
        if (username == readName(user.username, sizeof(user.username)) &&
            password == readName(user.password, sizeof(user.password))) {
            session_.loggedIn = true;
            session_.uid = user.uid;
            session_.gid = user.gid;
            session_.username = username;
            session_.homeInode = findHomeInode(username);
            if (session_.homeInode == kInvalidInode) {
                std::cout << "用户家目录不存在，请先执行 format 重新初始化文件卷。\n";
                session_ = UserSession{};
                return;
            }
            session_.cwdInode = session_.homeInode;
            session_.openFiles.fill(-1);
            std::cout << "登录成功，欢迎 " << username << "。\n";
            return;
        }
    }

    std::cout << "用户名或密码错误。\n";
}

void FileSystem::logout() {
    if (!session_.loggedIn) {
        std::cout << "当前没有用户登录。\n";
        return;
    }

    for (std::size_t fd = 0; fd < session_.openFiles.size(); ++fd) {
        if (session_.openFiles[fd] != -1) {
            closeFd(static_cast<int>(fd), false);
        }
    }

    std::cout << "用户 " << session_.username << " 已注销。\n";
    session_ = UserSession{};
    disk_.sync();
}

void FileSystem::createFile() {
    createNode(readLine("请输入待创建文件路径: "), InodeType::File);
}

void FileSystem::makeDirectory() {
    createNode(readLine("请输入待创建目录路径: "), InodeType::Directory);
}

void FileSystem::deleteFile() {
    removeNode(readLine("请输入待删除文件/目录路径: "));
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

void FileSystem::openFile() {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return;
    }

    const std::uint32_t inodeNo = resolvePath(readLine("请输入待打开文件路径: "));
    if (inodeNo == kInvalidInode) {
        std::cout << "文件不存在。\n";
        return;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::File)) {
        std::cout << "目标不是普通文件。\n";
        return;
    }

    const std::string modeText = readLine("模式(r/w/rw/a): ");
    OpenMode mode = OpenMode::Read;
    bool needRead = false;
    bool needWrite = false;
    if (modeText == "r") {
        mode = OpenMode::Read;
        needRead = true;
    } else if (modeText == "w") {
        mode = OpenMode::Write;
        needWrite = true;
    } else if (modeText == "rw") {
        mode = OpenMode::ReadWrite;
        needRead = true;
        needWrite = true;
    } else if (modeText == "a") {
        mode = OpenMode::Append;
        needWrite = true;
    } else {
        std::cout << "不支持的打开模式。\n";
        return;
    }

    if (!accessAllowed(inode, needRead, needWrite)) {
        std::cout << "权限不足，无法按该模式打开文件。\n";
        return;
    }

    const std::uint32_t offset = mode == OpenMode::Append ? inode.size : 0;
    const int sysIndex = allocateSystemOpen(mode, inodeNo, offset);
    if (sysIndex < 0) {
        std::cout << "系统打开文件表已满。\n";
        return;
    }

    const int fd = allocateUserFd(sysIndex);
    if (fd < 0) {
        openFiles_[sysIndex] = OpenFile{};
        std::cout << "当前用户打开文件数已达上限。\n";
        return;
    }

    std::cout << "打开成功，文件描述符 fd = " << fd << "。\n";
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

void FileSystem::closeFile() {
    ensureLoggedIn();
    if (session_.loggedIn) {
        closeFd(readInt("请输入待关闭 fd: "), true);
    }
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

void FileSystem::writeFile() {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return;
    }

    const int fd = readInt("请输入待写入 fd: ");
    if (fd < 0 || fd >= static_cast<int>(session_.openFiles.size()) || session_.openFiles[fd] == -1) {
        std::cout << "fd 无效。\n";
        return;
    }

    OpenFile& open = openFiles_[session_.openFiles[fd]];
    if (open.mode == OpenMode::Read) {
        std::cout << "该文件以只读方式打开，不能写入。\n";
        return;
    }

    std::vector<char> current;
    if (!readFileContent(open.inodeNo, current)) {
        std::cout << "读取原文件内容失败。\n";
        return;
    }

    const std::string text = readLine("请输入写入内容: ");
    std::vector<char> updated = current;
    if (open.mode == OpenMode::Write) {
        updated.assign(text.begin(), text.end());
        open.offset = static_cast<std::uint32_t>(updated.size());
    } else {
        const std::uint32_t start = open.offset;
        if (updated.size() < start) {
            updated.resize(start, '\0');
        }
        if (updated.size() < start + text.size()) {
            updated.resize(start + text.size(), '\0');
        }
        std::memcpy(updated.data() + start, text.data(), text.size());
        open.offset = start + static_cast<std::uint32_t>(text.size());
    }

    if (writeFileContent(open.inodeNo, updated)) {
        std::cout << "写入成功，当前文件大小 " << updated.size() << " 字节。\n";
    }
}

void FileSystem::readFile() {
    ensureLoggedIn();
    if (!session_.loggedIn) {
        return;
    }

    const int fd = readInt("请输入待读取 fd: ");
    if (fd < 0 || fd >= static_cast<int>(session_.openFiles.size()) || session_.openFiles[fd] == -1) {
        std::cout << "fd 无效。\n";
        return;
    }

    OpenFile& open = openFiles_[session_.openFiles[fd]];
    if (open.mode == OpenMode::Write || open.mode == OpenMode::Append) {
        std::cout << "当前打开模式不允许读取。\n";
        return;
    }

    std::vector<char> content;
    if (!readFileContent(open.inodeNo, content)) {
        std::cout << "读取失败。\n";
        return;
    }

    std::cout << "文件内容如下:\n" << std::string(content.begin(), content.end()) << '\n';
    open.offset = static_cast<std::uint32_t>(content.size());
}

void FileSystem::shutdown() {
    flushSuper();
    disk_.sync();
    std::cout << "文件系统状态已保存到 " << kDiskFileName << "。\n";
    std::cout << "默认账户: usr1~usr8，默认密码: pass1~pass8。\n";
}

}  // namespace vfs
