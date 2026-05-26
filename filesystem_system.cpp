#include "vfs.h"

#include <cstring>
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

void FileSystem::shutdown() {
    flushSuper();
    disk_.sync();
    std::cout << "文件系统状态已保存到 " << kDiskFileName << "。\n";
    std::cout << "默认账户: usr1~usr8，默认密码: pass1~pass8。\n";
}

}  // namespace vfs
