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

}  // namespace vfs
