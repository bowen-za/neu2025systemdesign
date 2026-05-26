#include "vfs.h"

#include <iostream>

namespace vfs {

bool FileSystem::accessAllowed(const DiskInode& inode, bool needRead, bool needWrite) const {
    if (!session_.loggedIn) {
        return false;
    }

    if (session_.isAdmin) {
        return true;
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
    if (username == "admin") {
        return kRootInode;
    }
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
            session_.isAdmin = (user.isAdmin != 0);
            session_.homeInode = findHomeInode(username);
            if (session_.homeInode == kInvalidInode) {
                std::cout << "用户家目录不存在，请先执行 format 重新初始化文件卷。\n";
                session_ = UserSession{};
                return;
            }
            session_.cwdInode = session_.homeInode;
            session_.openFiles.fill(-1);
            std::cout << "登录成功，欢迎 " << username;
            if (session_.isAdmin) {
                std::cout << " [管理员]";
            }
            std::cout << "。\n";
            return;
        }
    }

    std::cout << "用户名或密码错误。\n";
}

void FileSystem::adminLogin() {
    if (session_.loggedIn) {
        std::cout << "当前已有用户登录，请先注销。\n";
        return;
    }

    const std::string username = readLine("管理员用户名: ");
    const std::string password = readLine("管理员密码: ");
    for (const auto& user : users_) {
        if (user.isAdmin != 0 &&
            username == readName(user.username, sizeof(user.username)) &&
            password == readName(user.password, sizeof(user.password))) {
            session_.loggedIn = true;
            session_.uid = user.uid;
            session_.gid = user.gid;
            session_.username = username;
            session_.isAdmin = true;
            session_.cwdInode = kRootInode;
            session_.homeInode = kRootInode;
            session_.openFiles.fill(-1);
            std::cout << "管理员登录成功，欢迎 " << username << "。\n";
            return;
        }
    }

    std::cout << "管理员用户名或密码错误。\n";
}

void FileSystem::registerUser() {
    std::string username = readLine("请输入新用户名: ");
    if (username.empty()) {
        std::cout << "用户名不能为空。\n";
        return;
    }
    if (username.size() >= 16) {
        std::cout << "用户名过长。\n";
        return;
    }

    for (const auto& user : users_) {
        if (username == readName(user.username, sizeof(user.username))) {
            std::cout << "用户名已存在。\n";
            return;
        }
    }

    int freeIndex = -1;
    for (std::size_t i = 0; i < users_.size(); ++i) {
        if (readName(users_[i].username, sizeof(users_[i].username)).empty()) {
            freeIndex = static_cast<int>(i);
            break;
        }
    }

    if (freeIndex == -1) {
        std::cout << "用户数量已达上限。\n";
        return;
    }

    std::string password = readLine("请输入密码: ");
    if (password.empty()) {
        std::cout << "密码不能为空。\n";
        return;
    }
    if (password.size() >= 16) {
        std::cout << "密码过长。\n";
        return;
    }

    UserRecord newUser{};
    std::strncpy(newUser.username, username.c_str(), sizeof(newUser.username) - 1);
    std::strncpy(newUser.password, password.c_str(), sizeof(newUser.password) - 1);
    newUser.uid = static_cast<std::uint16_t>(freeIndex);
    newUser.gid = static_cast<std::uint16_t>(freeIndex);
    newUser.isAdmin = 0;
    users_[freeIndex] = newUser;

    writeUsers();

    std::vector<DirEntry> rootEntries;
    readDirectoryEntries(kRootInode, rootEntries);

    const int inodeNo = ialloc();
    const int blockNo = ballocInternal();
    if (inodeNo >= 0 && blockNo >= 0) {
        DiskInode home{};
        home.used = 1;
        home.type = static_cast<std::uint8_t>(InodeType::Directory);
        home.uid = newUser.uid;
        home.permissions = 0700;
        home.linkCount = 1;
        home.size = 0;
        home.direct[0] = static_cast<std::uint32_t>(blockNo);
        writeInode(static_cast<std::uint32_t>(inodeNo), home);

        writeDirectoryEntries(static_cast<std::uint32_t>(inodeNo),
                              makeInitialDirectory(static_cast<std::uint32_t>(inodeNo), kRootInode));
        rootEntries.push_back(makeDirEntry(username, static_cast<std::uint32_t>(inodeNo),
                                           InodeType::Directory));
        writeDirectoryEntries(kRootInode, rootEntries);
    }

    flushSuper();
    disk_.sync();
    std::cout << "用户注册成功！\n";
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
