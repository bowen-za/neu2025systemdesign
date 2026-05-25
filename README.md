<<<<<<< HEAD
# 模拟 UNIX 文件系统课程设计

这是一个用 C++ 实现的“多用户、多级目录结构文件系统”的课程设计项目，用一个宿主文件 `virtual_disk.bin` 来模拟磁盘，保存文件系统的全部状态。程序启动后可以进行登录、创建目录、创建文件、读写文件、删除文件、切换目录、列目录等操作，并且支持退出后再次启动时恢复上次的数据。

## 项目目标

这个项目的核心目标是模拟 UNIX 文件系统的基本组织方式，而不是依赖真实操作系统文件接口直接做普通文件管理。项目内部实现了：

1. 超级块 `SuperBlock`
2. 磁盘 inode 与内存 inode
3. 目录项 `DirEntry`
4. 空闲块管理
5. 用户登录与会话状态
6. 系统打开文件表与用户打开文件表
7. 虚拟磁盘持久化

## 功能列表

当前程序支持以下功能：

1. `format`：格式化文件系统，重新建立文件卷
2. `login`：用户登录
3. `logout`：用户注销
4. `create`：创建普通文件
5. `open`：打开文件
6. `read`：读取文件内容
7. `write`：向文件写入内容
8. `close`：关闭文件
9. `delete`：删除文件或目录
10. `mkdir`：创建目录
11. `chdir`：切换当前目录
12. `dir`：列出目录内容
13. `exit`：退出并保存状态

## 多用户设计

程序内置了 8 个默认用户：

- `usr1` / `pass1`
- `usr2` / `pass2`
- `usr3` / `pass3`
- `usr4` / `pass4`
- `usr5` / `pass5`
- `usr6` / `pass6`
- `usr7` / `pass7`
- `usr8` / `pass8`

格式化后会自动创建对应的用户家目录：

- `/usr1`
- `/usr2`
- `/usr3`
- `/usr4`
- `/usr5`
- `/usr6`
- `/usr7`
- `/usr8`

用户登录后会默认进入自己的家目录，因此不同用户的文件不会默认混在一起。

## 文件结构

项目已经按功能拆分为多个源文件：

- [main.cpp](./main.cpp)：程序入口，只负责初始化和启动主循环
- [vfs.h](./vfs.h)：公共头文件，包含常量、数据结构和类声明
- [utils.cpp](./utils.cpp)：字符串工具、控制台编码、用户会话初始化
- [virtual_disk.cpp](./virtual_disk.cpp)：虚拟磁盘文件的读写与块操作
- [filesystem.cpp](./filesystem.cpp)：文件系统核心逻辑
- [CMakeLists.txt](./CMakeLists.txt)：CMake 构建配置

## 虚拟磁盘说明

`virtual_disk.bin` 是程序模拟出来的“磁盘镜像文件”。程序会把超级块、inode、目录、文件内容和用户信息都保存到这个文件里。

如果你想重新开始测试，直接删除 `virtual_disk.bin` 即可。程序下次启动时会重新格式化并建立新的文件卷。

## 编译与运行

### 使用 CMake

```bash
cmake -S . -B build
cmake --build build
```

生成的可执行文件会在 `build` 目录中。

### 直接运行

如果已经有编译好的可执行文件，可以直接运行：

```bash
./vfs_unix_simulator
```

在 Windows 下也可以直接双击 `vfs_unix_simulator.exe`。

## 测试流程

一个最常见的测试流程如下：

```text
1. 启动程序
2. 选择 format，或删除 virtual_disk.bin 后重新启动
3. 登录 usr1 / pass1
4. 查看 /usr1 目录
5. 创建文件 notes.txt
6. open notes.txt，模式选择 rw
7. write 写入 hello
8. read 验证内容
9. logout
10. 登录 usr2 / pass2
11. 验证 usr2 默认进入 /usr2，且看不到 usr1 的文件
```

## 设计特点

- 使用菜单式交互，符合课程设计验收习惯
- 采用 C++ 分文件组织，便于阅读和维护
- 支持文件系统状态持久化
- 支持多用户、多级目录和基本权限控制
- 内部结构接近 UNIX 文件系统的教学模型

## 备注

本项目面向操作系统课程设计展示和教学演示，重点是“文件系统工作原理的模拟”，不是完整的真实 UNIX 实现。
=======
# neu2025systemdesign
>>>>>>> 9d5fa29138ee6347ba60d1798095a1e2785ff2f4
