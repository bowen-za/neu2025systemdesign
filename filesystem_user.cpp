#include "vfs.h"

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

void FileSystem::ensureLoggedIn() const {
    if (!session_.loggedIn) {
        std::cout << "请先登录。\n";
    }
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

}  // namespace vfs
