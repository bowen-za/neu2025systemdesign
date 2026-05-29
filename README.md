# BYSX OS 模拟 UNIX 文件系统

这是一个基于 C++ 和 Qt Widgets 实现的操作系统课程设计项目，主题为“多用户、多级目录结构文件系统的设计与实现”。项目将文件系统状态保存在宿主机上的 `virtual_disk.bin` 中，用它模拟一块虚拟磁盘，并在程序内部实现超级块、inode、目录项、数据块、用户登录、权限控制和打开文件表等 UNIX 风格文件系统机制。

系统名称为 **BYSX OS**，中文含义来自项目成员姓名：张博文、陈泽岳、曹晟嘉、贺思翔，分别取“博、岳、晟、翔”组成“博岳晟翔 OS”。

## 功能特点

- Qt 图形化界面，登录后进入类 Windows 桌面。
- 多用户登录，默认用户为 `usr1` 到 `usr8`，密码为 `pass1` 到 `pass8`。
- 每个用户登录后进入自己的用户目录，例如 `usr1` 进入 `/usr1`。
- 支持多级目录结构，目录以窗口形式打开，目录内文件以图标显示。
- 支持右键桌面或目录空白处新建 `.txt` 文件和目录。
- 支持右键文件/目录图标进行打开、删除、刷新操作。
- 打开文件时可选择只读、写入、读写、追加模式。
- 打开文件后提供内容编辑窗口，可读取和写入文件内容。
- 底部系统图标右键提供格式化、注销、退出功能。
- 使用 `virtual_disk.bin` 持久化保存文件卷，下次启动可恢复。

## 默认用户

格式化后会自动创建 8 个默认用户和对应家目录：

| 用户名 | 密码 |
| --- | --- |
| `usr1` | `pass1` |
| `usr2` | `pass2` |
| `usr3` | `pass3` |
| `usr4` | `pass4` |
| `usr5` | `pass5` |
| `usr6` | `pass6` |
| `usr7` | `pass7` |
| `usr8` | `pass8` |

## 虚拟磁盘说明

`virtual_disk.bin` 是程序运行时生成的虚拟磁盘镜像文件，不是真实硬盘分区。程序会把超级块、用户表、inode 区、目录项、文件内容和空闲块状态写入这个文件。

该文件属于运行数据，不建议提交到 GitHub。仓库中的 `.gitignore` 已经排除了它。需要重新初始化系统时，可以在登录界面或桌面系统图标菜单中选择“格式化”。

## 项目结构

| 文件 | 说明 |
| --- | --- |
| `vfs.h` | 文件系统常量、磁盘结构体、inode、目录项、用户会话和类声明 |
| `virtual_disk.cpp` | 虚拟磁盘文件的创建、加载、同步和按块读写 |
| `filesystem_system.cpp` | 初始化、格式化、超级块、空闲块和 inode 管理 |
| `filesystem_user.cpp` | 用户目录定位和基础权限判断 |
| `filesystem_directory.cpp` | 目录项读写、路径解析、目录创建和切换 |
| `filesystem_file.cpp` | 文件创建、删除、打开、读写和关闭 |
| `filesystem_api.cpp` | 提供给 Qt 界面调用的文件系统 API |
| `utils.cpp` | 字符串处理、名称拷贝、控制台编码辅助 |
| `qt_main.cpp` | Qt 程序入口 |
| `qt_mainwindow.h/.cpp` | BYSX OS 图形界面、登录页、桌面、右键菜单和文件窗口 |
| `resources.qrc` | Qt 资源文件，嵌入图标和壁纸 |
| `assets/` | BYSX OS 图标和桌面壁纸 |
| `CMakeLists.txt` | CMake 构建配置 |

## 构建环境

推荐环境：

- Windows
- Qt 6.x MinGW 64-bit
- Qt 自带 CMake
- C++11 或更高标准

如果使用 Qt Creator，直接打开 `CMakeLists.txt`，选择 Desktop Qt MinGW Kit，然后构建运行即可。

命令行构建示例：

```powershell
cmake -S . -B build-qt -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:/qt/6.11.1/mingw_64" -DQt6_DIR="D:/qt/6.11.1/mingw_64/lib/cmake/Qt6" -DCMAKE_CXX_COMPILER="D:/qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build-qt -j 8
```

生成程序：

```text
build-qt/vfs_unix_simulator.exe
```

如果要直接双击运行，需要确保 Qt DLL 已部署到 exe 同目录，可使用：

```powershell
windeployqt --compiler-runtime build-qt\vfs_unix_simulator.exe
```

## 操作流程

1. 启动程序，进入 BYSX OS 登录界面。
2. 首次测试可点击“格式化”，初始化虚拟磁盘和默认用户。
3. 使用 `usr1 / pass1` 登录。
4. 登录后进入 `/usr1` 用户桌面。
5. 在桌面空白处右键，选择“新建文件”或“新建目录”。
6. 新建文件名必须以 `.txt` 结尾，例如 `notes.txt`。
7. 右键文件图标，选择“打开”，再选择打开模式。
8. 在文件窗口中读取或写入内容。
9. 右键目录图标，选择“打开”，会弹出目录窗口。
10. 右键底部系统图标，可执行格式化、注销或退出。

## 测试建议

推荐验收测试流程：

```text
格式化
登录 usr1 / pass1
在 /usr1 新建 notes.txt
以 rw 模式打开 notes.txt
写入 hello bysx
关闭文件窗口
重新打开 notes.txt 并读取内容
注销 usr1
登录 usr2 / pass2
确认进入 /usr2，默认看不到 usr1 的文件
退出程序后重新启动
确认文件系统状态可从 virtual_disk.bin 恢复
```

## Git 提交说明

仓库会提交源码、README、Qt 资源文件和项目图片资源。以下内容已通过 `.gitignore` 排除，不应提交：

- `build/`、`build-*`、`cmake-build-*`
- `*.exe`、`*.dll`、`*.obj`、`*.lib`、`*.pdb`
- `virtual_disk.bin`、`*.bin`
- `.qtcreator/`、`.qtc_clangd/`、`.vscode/`、`.idea/`

提交前可检查：

```powershell
git status --short
```

## 课程设计说明

本项目重点展示文件系统内部机制，而不是调用真实操作系统文件 API 完成文件管理。文件内容、目录结构、用户信息和分配状态都保存在虚拟磁盘中，由程序自行管理。

当前版本采用固定数量直接块地址实现文件存储，未实现完整 UNIX 多级索引；权限控制采用课程设计级别的基础属主/非属主读写判断，适合课程演示和答辩说明。
