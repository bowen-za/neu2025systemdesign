#include "vfs.h"

#include <cstring>
#include <iostream>

namespace vfs {

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

bool FileSystem::registerUser(const std::string& username, const std::string& password, std::string& message) {
    if (session_.loggedIn) {
        message = "当前已有用户登录，请先注销后再注册。";
        return false;
    }

    if (username.empty() || password.empty()) {
        message = "用户名和密码不能为空。";
        return false;
    }

    if (username.size() >= 16) {
        message = "用户名不能超过15个字符。";
        return false;
    }

    if (password.size() >= 16) {
        message = "密码不能超过15个字符。";
        return false;
    }

    for (const auto& user : users_) {
        if (username == readName(user.username, sizeof(user.username))) {
            message = "用户名已存在。";
            return false;
        }
    }

    if (users_.size() >= kMaxUserCount) {
        message = "用户数量已达上限。";
        return false;
    }

    std::uint16_t maxUid = 0;
    for (const auto& user : users_) {
        if (user.uid > maxUid) {
            maxUid = user.uid;
        }
    }
    const std::uint16_t newUid = maxUid + 1;

    const int inodeNo = ialloc();
    if (inodeNo < 0) {
        message = "无法分配 inode。";
        return false;
    }

    const int blockNo = ballocInternal();
    if (blockNo < 0) {
        ifree(static_cast<std::uint32_t>(inodeNo));
        message = "磁盘空间不足。";
        return false;
    }

    UserRecord record{};
    std::strncpy(record.username, username.c_str(), sizeof(record.username) - 1);
    std::strncpy(record.password, password.c_str(), sizeof(record.password) - 1);
    record.uid = newUid;
    record.gid = newUid;
    users_.push_back(record);

    DiskInode home{};
    home.used = 1;
    home.type = static_cast<std::uint8_t>(InodeType::Directory);
    home.uid = newUid;
    home.permissions = 0700;
    home.linkCount = 1;
    home.size = 0;
    home.direct[0] = static_cast<std::uint32_t>(blockNo);
    writeInode(static_cast<std::uint32_t>(inodeNo), home);
    writeDirectoryEntries(static_cast<std::uint32_t>(inodeNo),
                          makeInitialDirectory(static_cast<std::uint32_t>(inodeNo), kRootInode));

    std::vector<DirEntry> rootEntries;
    readDirectoryEntries(kRootInode, rootEntries);
    rootEntries.push_back(makeDirEntry(username, static_cast<std::uint32_t>(inodeNo),
                                       InodeType::Directory));
    writeDirectoryEntries(kRootInode, rootEntries);

    writeUsers();
    disk_.sync();
    message = "注册成功。";
    return true;
}

}  // namespace vfs
