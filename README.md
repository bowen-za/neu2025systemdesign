# 模拟 UNIX 文件系统课程设计

这是一个用 C++ 和 Qt 实现的“多用户、多级目录结构文件系统”课程设计项目。系统使用 `virtual_disk.bin` 作为虚拟磁盘镜像文件，在其中保存超级块、inode、目录、文件内容和用户信息。

当前版本只保留 Qt 图形界面，不再提供控制台菜单版。图形界面采用类似 Windows 文件管理器的布局，包含用户登录区、路径栏、文件列表、工具栏、打开文件 fd 列表、内容编辑区和操作日志。

## 项目目标

项目模拟 UNIX 文件系统的基本组织方式，重点展示文件系统内部结构和操作流程，而不是直接调用真实操作系统文件管理功能。

已经实现的核心结构包括：

1. 超级块 `SuperBlock`
2. 磁盘 inode 与内存 inode
3. 目录项 `DirEntry`
4. 空闲块管理
5. 用户登录与会话状态
6. 系统打开文件表与用户打开文件表
7. 虚拟磁盘持久化
8. Qt 图形化操作界面

## 功能列表

当前图形界面支持：

1. `format`：格式化文件系统，重新建立文件卷
2. `login`：用户登录
3. `logout`：用户注销
4. `create`：创建普通文件
5. `open`：打开文件
6. `read`：读取文件内容
7. `write`：向文件写入内容
8. `close`：关闭文件描述符
9. `delete`：删除文件或空目录
10. `mkdir`：创建目录
11. `chdir`：切换当前目录
12. `dir`：以表格方式列出目录内容

## 默认用户

程序内置 8 个默认用户：

- `usr1` / `pass1`
- `usr2` / `pass2`
- `usr3` / `pass3`
- `usr4` / `pass4`
- `usr5` / `pass5`
- `usr6` / `pass6`
- `usr7` / `pass7`
- `usr8` / `pass8`

格式化后会自动创建 `/usr1` 到 `/usr8` 这些用户家目录。用户登录后默认进入自己的家目录，不同用户的文件默认隔离。

## 文件结构

- [vfs.h](./vfs.h)：公共头文件，包含常量、磁盘结构、inode、目录项和类声明
- [utils.cpp](./utils.cpp)：字符串工具、控制台编码和用户会话初始化
- [virtual_disk.cpp](./virtual_disk.cpp)：虚拟磁盘文件的加载、保存、按块读写
- [filesystem_system.cpp](./filesystem_system.cpp)：系统初始化、格式化、超级块、空闲块和 inode 管理
- [filesystem_user.cpp](./filesystem_user.cpp)：用户家目录定位和权限判断
- [filesystem_directory.cpp](./filesystem_directory.cpp)：目录项读写、路径解析、目录创建和目录切换
- [filesystem_file.cpp](./filesystem_file.cpp)：文件创建、删除、打开文件表和文件内容读写
- [filesystem_api.cpp](./filesystem_api.cpp)：给 Qt 图形界面调用的文件系统 API
- [qt_main.cpp](./qt_main.cpp)：Qt 程序入口
- [qt_mainwindow.h](./qt_mainwindow.h)：Qt 主窗口声明
- [qt_mainwindow.cpp](./qt_mainwindow.cpp)：仿 Windows 文件管理器风格的图形界面
- [CMakeLists.txt](./CMakeLists.txt)：CMake 构建配置

## 虚拟磁盘

`virtual_disk.bin` 是系统的虚拟硬盘镜像。程序启动时会从它加载文件系统状态，退出或保存时会把当前状态写回它。

如果想从空白状态重新测试，可以删除 `virtual_disk.bin`，或者在图形界面中点击“格式化”。

## 编译与运行

需要先安装 Qt5 或 Qt6 Widgets，以及 CMake。

使用 CMake 构建：

```bash
cmake -S . -B build
cmake --build build
```

生成的程序目标名为：

```text
vfs_unix_simulator
```

在 Qt Creator 中打开 [CMakeLists.txt](./CMakeLists.txt)，选择 Desktop Qt Kit 后直接构建并运行即可。

## 测试流程

推荐测试流程：

```text
1. 启动图形界面
2. 点击“格式化”
3. 使用 usr1 / pass1 登录
4. 在 /usr1 下创建 notes.txt
5. 选择 notes.txt，使用 rw 模式打开
6. 在内容编辑区输入 hello
7. 点击“写入”
8. 点击“读取”，确认内容显示为 hello
9. 注销 usr1
10. 使用 usr2 / pass2 登录
11. 确认 usr2 默认进入 /usr2，看不到 usr1 的文件
12. 尝试进入 /usr1，应该提示没有权限
```

## 说明

本项目面向操作系统课程设计展示，重点是模拟文件系统内部工作原理。当前文件物理结构采用 10 个直接块，未实现完整 UNIX 混合索引。
