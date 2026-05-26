#ifndef VFS_H
#define VFS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vfs {

constexpr std::uint32_t kBlockSize = 512;
constexpr std::uint32_t kInodeBlocks = 32;
constexpr std::uint32_t kDataBlocks = 512;
constexpr std::uint32_t kBootBlock = 0;
constexpr std::uint32_t kSuperBlockNo = 1;
constexpr std::uint32_t kInodeStartBlock = 2;
constexpr std::uint32_t kDataStartBlock = kInodeStartBlock + kInodeBlocks;
constexpr std::uint32_t kTotalBlocks = kDataStartBlock + kDataBlocks;
constexpr std::uint32_t kMagic = 0x56465331;

constexpr std::uint32_t kUserCount = 8;
constexpr std::uint32_t kNicFree = 50;
constexpr std::uint32_t kNicInode = 50;
constexpr std::uint32_t kNAddr = 10;
constexpr std::uint32_t kSystemOpenMax = 40;
constexpr std::uint32_t kUserOpenMax = 20;
constexpr std::uint32_t kNameSize = 27;
constexpr std::uint32_t kMaxFileSize = kBlockSize * kNAddr;
constexpr std::uint32_t kRootInode = 0;
constexpr std::uint32_t kInvalidInode = 0xFFFFFFFFu;

extern const char kDiskFileName[];

enum class InodeType : std::uint8_t {
    Free = 0,
    File = 1,
    Directory = 2,
};

enum class OpenMode : std::uint8_t {
    Read = 1,
    Write = 2,
    ReadWrite = 3,
    Append = 4,
};

#pragma pack(push, 1)
struct UserRecord {
    char username[16];
    char password[16];
    std::uint16_t uid;
    std::uint16_t gid;
    std::uint8_t isAdmin;
    std::uint8_t reserved[27];
};

struct GroupBlock {
    std::uint32_t count;
    std::uint32_t blocks[kNicFree];
    std::uint8_t reserved[kBlockSize - sizeof(std::uint32_t) * (1 + kNicFree)];
};

struct DiskInode {
    std::uint8_t used;
    std::uint8_t type;
    std::uint16_t uid;
    std::uint16_t permissions;
    std::uint16_t linkCount;
    std::uint32_t size;
    std::uint32_t direct[kNAddr];
    std::uint8_t reserved[12];
};

struct DirEntry {
    std::uint32_t inode;
    std::uint8_t type;
    char name[kNameSize];
};

struct SuperBlock {
    std::uint32_t magic;
    std::uint32_t totalBlocks;
    std::uint32_t inodeStartBlock;
    std::uint32_t inodeBlocks;
    std::uint32_t dataStartBlock;
    std::uint32_t dataBlocks;
    std::uint32_t inodeCount;
    std::uint32_t rootInode;
    std::uint32_t freeBlockCount;
    std::uint32_t freeBlockStack[kNicFree];
    std::uint32_t freeInodeCount;
    std::uint32_t freeInodeStack[kNicInode];
    std::uint32_t nextInodeHint;
    std::uint32_t userCount;
    std::uint32_t dirty;
    std::uint8_t reserved[60];
};
#pragma pack(pop)

static_assert(sizeof(UserRecord) * kUserCount == kBlockSize, "user records must fill block 0");
static_assert(sizeof(GroupBlock) == kBlockSize, "group block must occupy one block");
static_assert(sizeof(DiskInode) == 64, "disk inode must be 64 bytes");
static_assert(sizeof(DirEntry) == 32, "dir entry must be 32 bytes");
static_assert(sizeof(SuperBlock) == kBlockSize, "super block must occupy one block");

struct MemInode {
    DiskInode disk{};
    std::uint32_t inodeNo = 0;
    std::uint32_t refCount = 0;
    bool dirty = false;
};

struct OpenFile {
    bool used = false;
    OpenMode mode = OpenMode::Read;
    std::uint32_t inodeNo = 0;
    std::uint32_t offset = 0;
    std::uint32_t refCount = 0;
};

struct UserSession {
    bool loggedIn = false;
    std::uint16_t uid = 0;
    std::uint16_t gid = 0;
    std::string username;
    bool isAdmin = false;
    std::uint32_t cwdInode = kRootInode;
    std::uint32_t homeInode = kRootInode;
    std::array<int, kUserOpenMax> openFiles{};

    UserSession();
};

std::string trim(const std::string& input);
bool splitParent(const std::string& path, std::string& parent, std::string& leaf);
void copyName(char* dest, const std::string& name);
std::string readName(const char* src, std::size_t len);
void configureConsoleEncoding();

class VirtualDisk {
public:
    bool exists() const;
    bool load();
    void createEmpty();
    bool sync() const;

    template <typename T>
    void readStruct(std::uint32_t blockNo, T& out) const {
        readBytes(blockNo * kBlockSize, &out, sizeof(T));
    }

    template <typename T>
    void writeStruct(std::uint32_t blockNo, const T& in) {
        writeBytes(blockNo * kBlockSize, &in, sizeof(T));
    }

    void readBytes(std::uint32_t offset, void* out, std::size_t size) const;
    void writeBytes(std::uint32_t offset, const void* data, std::size_t size);
    void zeroBlock(std::uint32_t blockNo);
    const char* blockPtr(std::uint32_t blockNo) const;
    char* blockPtr(std::uint32_t blockNo);

private:
    std::vector<char> bytes_;
};

class FileSystem {
public:
    bool initialize();
    bool format();
    void run();

private:
    void initializeUsers();
    void loadUsers();
    void writeUsers();
    void createUserHomes();
    void clearInodeArea();
    void flushSuper();

    std::uint32_t inodeOffset(std::uint32_t inodeNo) const;
    DiskInode readInode(std::uint32_t inodeNo) const;
    void writeInode(std::uint32_t inodeNo, const DiskInode& inode);
    int ballocInternal();
    void bfreeInternal(std::uint32_t blockNo);
    void rebuildFreeInodeStack();
    int ialloc();
    void ifree(std::uint32_t inodeNo);
    MemInode* iget(std::uint32_t inodeNo);
    void iput(MemInode* inode);

    std::vector<DirEntry> makeInitialDirectory(std::uint32_t self, std::uint32_t parent);
    DirEntry makeDirEntry(const std::string& name, std::uint32_t inodeNo, InodeType type);
    bool readDirectoryEntries(std::uint32_t inodeNo, std::vector<DirEntry>& entries) const;
    bool writeDirectoryEntries(std::uint32_t inodeNo, const std::vector<DirEntry>& entries);
    bool findEntry(std::uint32_t dirInode, const std::string& name, DirEntry& foundEntry) const;
    std::uint32_t resolvePath(const std::string& path) const;
    bool accessAllowed(const DiskInode& inode, bool needRead, bool needWrite) const;
    std::uint32_t findHomeInode(const std::string& username) const;

    bool createNode(const std::string& path, InodeType type);
    bool removeNode(const std::string& path);
    int allocateSystemOpen(OpenMode mode, std::uint32_t inodeNo, std::uint32_t offset);
    int allocateUserFd(int sysIndex);
    void ensureLoggedIn() const;
    void printMenu() const;
    int readInt(const std::string& prompt) const;
    std::string readLine(const std::string& prompt) const;
    std::string currentPath() const;
    std::string findNameInParent(std::uint32_t parentInode, std::uint32_t childInode) const;

    void login();
    void logout();
    void registerUser();
    void adminLogin();
    void createFile();
    void makeDirectory();
    void deleteFile();
    void changeDirectory();
    void listDirectory();
    void openFile();
    bool closeFd(int fd, bool verbose);
    void closeFile();
    bool readFileContent(std::uint32_t inodeNo, std::vector<char>& content) const;
    bool writeFileContent(std::uint32_t inodeNo, const std::vector<char>& content);
    void writeFile();
    void readFile();
    void shutdown();

    VirtualDisk disk_;
    SuperBlock super_{};
    std::vector<UserRecord> users_;
    std::vector<OpenFile> openFiles_;
    UserSession session_;
    std::map<std::uint32_t, MemInode> memInodes_;
};

}  // namespace vfs

#endif
