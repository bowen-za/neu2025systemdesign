#include "vfs.h"

#include <iostream>

int main() {
    vfs::configureConsoleEncoding();

    vfs::FileSystem fs;
    if (!fs.initialize()) {
        std::cerr << "文件系统初始化失败。\n";
        return 1;
    }

    fs.run();
    return 0;
}
