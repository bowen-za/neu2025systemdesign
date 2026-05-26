#include "vfs.h"

#include <algorithm>

namespace vfs {

bool FileSystem::isLoggedIn() const {
    return session_.loggedIn;
}

std::string FileSystem::currentUserName() const {
    return session_.loggedIn ? session_.username : "";
}

std::string FileSystem::currentDirectoryPath() const {
    return currentPath();
}

bool FileSystem::loginUser(const std::string& username, const std::string& password, std::string& message) {
    if (session_.loggedIn) {
        message = "当前已有用户登录，请先注销。";
        return false;
    }

    for (const auto& user : users_) {
        if (username == readName(user.username, sizeof(user.username)) &&
            password == readName(user.password, sizeof(user.password))) {
            session_.loggedIn = true;
            session_.uid = user.uid;
            session_.gid = user.gid;
            session_.username = username;
            session_.homeInode = findHomeInode(username);
            if (session_.homeInode == kInvalidInode) {
                session_ = UserSession{};
                message = "用户家目录不存在，请先格式化文件卷。";
                return false;
            }

            session_.cwdInode = session_.homeInode;
            session_.openFiles.fill(-1);
            message = "登录成功。";
            return true;
        }
    }

    message = "用户名或密码错误。";
    return false;
}

bool FileSystem::logoutUser(std::string& message) {
    if (!session_.loggedIn) {
        message = "当前没有用户登录。";
        return false;
    }

    for (std::size_t fd = 0; fd < session_.openFiles.size(); ++fd) {
        if (session_.openFiles[fd] != -1) {
            closeFd(static_cast<int>(fd), false);
        }
    }

    session_ = UserSession{};
    disk_.sync();
    message = "已注销。";
    return true;
}

bool FileSystem::createFileAt(const std::string& path, std::string& message) {
    const bool ok = createNode(path, InodeType::File);
    message = ok ? "文件创建成功。" : "文件创建失败，请检查路径、权限或同名文件。";
    return ok;
}

bool FileSystem::createDirectoryAt(const std::string& path, std::string& message) {
    const bool ok = createNode(path, InodeType::Directory);
    message = ok ? "目录创建成功。" : "目录创建失败，请检查路径、权限或同名目录。";
    return ok;
}

bool FileSystem::deleteAt(const std::string& path, std::string& message) {
    const bool ok = removeNode(path);
    message = ok ? "删除成功。" : "删除失败，请检查路径、权限、目录是否为空或文件是否仍打开。";
    return ok;
}

bool FileSystem::changeDirectoryTo(const std::string& path, std::string& message) {
    if (!session_.loggedIn) {
        message = "请先登录。";
        return false;
    }

    const std::uint32_t inodeNo = resolvePath(path);
    if (inodeNo == kInvalidInode) {
        message = "路径不存在。";
        return false;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        message = "目标不是目录。";
        return false;
    }
    if (!accessAllowed(inode, true, false)) {
        message = "没有进入该目录的权限。";
        return false;
    }

    session_.cwdInode = inodeNo;
    message = "目录切换成功。";
    return true;
}

std::vector<DirectoryViewItem> FileSystem::listDirectoryAt(const std::string& path, std::string& message) const {
    std::vector<DirectoryViewItem> result;
    if (!session_.loggedIn) {
        message = "请先登录。";
        return result;
    }

    const std::uint32_t inodeNo = resolvePath(path.empty() ? "." : path);
    if (inodeNo == kInvalidInode) {
        message = "目录不存在。";
        return result;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::Directory)) {
        message = "目标不是目录。";
        return result;
    }
    if (!accessAllowed(inode, true, false)) {
        message = "没有读取目录的权限。";
        return result;
    }

    std::vector<DirEntry> entries;
    if (!readDirectoryEntries(inodeNo, entries)) {
        message = "目录读取失败。";
        return result;
    }

    for (const auto& entry : entries) {
        const DiskInode child = readInode(entry.inode);
        DirectoryViewItem item;
        item.name = readName(entry.name, kNameSize);
        item.inodeNo = entry.inode;
        item.size = child.size;
        item.permissions = child.permissions;
        item.type = child.type == static_cast<std::uint8_t>(InodeType::Directory)
                        ? InodeType::Directory
                        : InodeType::File;
        result.push_back(item);
    }

    message = "目录读取成功。";
    return result;
}

bool FileSystem::openFileAt(const std::string& path, OpenMode mode, int& fd, std::string& message) {
    fd = -1;
    if (!session_.loggedIn) {
        message = "请先登录。";
        return false;
    }

    const std::uint32_t inodeNo = resolvePath(path);
    if (inodeNo == kInvalidInode) {
        message = "文件不存在。";
        return false;
    }

    const DiskInode inode = readInode(inodeNo);
    if (inode.type != static_cast<std::uint8_t>(InodeType::File)) {
        message = "目标不是普通文件。";
        return false;
    }

    const bool needRead = mode == OpenMode::Read || mode == OpenMode::ReadWrite;
    const bool needWrite = mode == OpenMode::Write || mode == OpenMode::ReadWrite || mode == OpenMode::Append;
    if (!accessAllowed(inode, needRead, needWrite)) {
        message = "权限不足，无法按该模式打开文件。";
        return false;
    }

    const std::uint32_t offset = mode == OpenMode::Append ? inode.size : 0;
    const int sysIndex = allocateSystemOpen(mode, inodeNo, offset);
    if (sysIndex < 0) {
        message = "系统打开文件表已满。";
        return false;
    }

    fd = allocateUserFd(sysIndex);
    if (fd < 0) {
        openFiles_[sysIndex] = OpenFile{};
        message = "当前用户打开文件数已达上限。";
        return false;
    }

    message = "打开成功。";
    return true;
}

bool FileSystem::closeDescriptor(int fd, std::string& message) {
    const bool ok = closeFd(fd, false);
    message = ok ? "文件已关闭。" : "fd 无效。";
    return ok;
}

bool FileSystem::writeDescriptor(int fd, const std::string& text, std::string& message) {
    if (fd < 0 || fd >= static_cast<int>(session_.openFiles.size()) || session_.openFiles[fd] == -1) {
        message = "fd 无效。";
        return false;
    }

    OpenFile& open = openFiles_[session_.openFiles[fd]];
    if (open.mode == OpenMode::Read) {
        message = "该文件以只读方式打开，不能写入。";
        return false;
    }

    std::vector<char> current;
    if (!readFileContent(open.inodeNo, current)) {
        message = "读取原文件内容失败。";
        return false;
    }

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
        std::copy(text.begin(), text.end(), updated.begin() + start);
        open.offset = start + static_cast<std::uint32_t>(text.size());
    }

    if (!writeFileContent(open.inodeNo, updated)) {
        message = "写入失败。";
        return false;
    }

    message = "写入成功，当前文件大小 " + std::to_string(updated.size()) + " 字节。";
    return true;
}

bool FileSystem::readDescriptor(int fd, std::string& content, std::string& message) {
    content.clear();
    if (fd < 0 || fd >= static_cast<int>(session_.openFiles.size()) || session_.openFiles[fd] == -1) {
        message = "fd 无效。";
        return false;
    }

    OpenFile& open = openFiles_[session_.openFiles[fd]];
    if (open.mode == OpenMode::Write || open.mode == OpenMode::Append) {
        message = "当前打开模式不允许读取。";
        return false;
    }

    std::vector<char> bytes;
    if (!readFileContent(open.inodeNo, bytes)) {
        message = "读取失败。";
        return false;
    }

    content.assign(bytes.begin(), bytes.end());
    open.offset = static_cast<std::uint32_t>(bytes.size());
    message = "读取成功。";
    return true;
}

bool FileSystem::save(std::string& message) {
    flushSuper();
    const bool ok = disk_.sync();
    message = ok ? "文件系统状态已保存。" : "保存失败。";
    return ok;
}

}  // namespace vfs
