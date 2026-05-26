#include "qt_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QAbstractItemView>

namespace {

QString toQString(const std::string& value) {
    return QString::fromUtf8(value.c_str());
}

std::string toStdString(const QString& value) {
    return value.toUtf8().constData();
}

QString typeName(vfs::InodeType type) {
    return type == vfs::InodeType::Directory ? "文件夹" : "文件";
}

QString permissionText(std::uint16_t permissions) {
    return QString::number(permissions, 8);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    std::string message;
    fs_.initialize();
    buildUi();
    connectActions();
    refreshState();
    appendLog("图形界面已启动。");
}

void MainWindow::buildUi() {
    setWindowTitle("模拟 UNIX 文件系统");
    setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));

    auto* formatAction = new QAction("格式化", this);
    auto* refreshAction = new QAction("刷新", this);
    auto* exitAction = new QAction("退出", this);
    menuBar()->addAction(formatAction);
    menuBar()->addAction(refreshAction);
    menuBar()->addAction(exitAction);

    auto* toolbar = addToolBar("文件系统");
    toolbar->setMovable(false);
    toolbar->addAction(formatAction);
    toolbar->addAction(refreshAction);
    toolbar->addSeparator();

    auto* createFileAction = toolbar->addAction("新建文件");
    auto* createDirAction = toolbar->addAction("新建文件夹");
    auto* deleteAction = toolbar->addAction("删除");
    auto* openAction = toolbar->addAction("打开");
    auto* readAction = toolbar->addAction("读取");
    auto* writeAction = toolbar->addAction("写入");
    auto* closeAction = toolbar->addAction("关闭 fd");

    userEdit_ = new QLineEdit("usr1");
    passwordEdit_ = new QLineEdit("pass1");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    auto* loginButton = new QPushButton("登录");
    auto* logoutButton = new QPushButton("注销");

    auto* loginPanel = new QWidget;
    auto* loginLayout = new QVBoxLayout(loginPanel);
    loginLayout->addWidget(new QLabel("用户"));
    loginLayout->addWidget(userEdit_);
    loginLayout->addWidget(new QLabel("密码"));
    loginLayout->addWidget(passwordEdit_);
    loginLayout->addWidget(loginButton);
    loginLayout->addWidget(logoutButton);
    loginLayout->addStretch();

    userLabel_ = new QLabel;
    pathLabel_ = new QLabel;
    pathEdit_ = new QLineEdit(".");
    auto* cdButton = new QPushButton("转到");

    fileTable_ = new QTableWidget(0, 5);
    fileTable_->setHorizontalHeaderLabels(QStringList() << "名称" << "类型" << "inode" << "大小" << "权限");
    fileTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    fileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTable_->verticalHeader()->setVisible(false);
    fileTable_->setAlternatingRowColors(true);

    modeCombo_ = new QComboBox;
    modeCombo_->addItem("只读 r", static_cast<int>(vfs::OpenMode::Read));
    modeCombo_->addItem("写入 w", static_cast<int>(vfs::OpenMode::Write));
    modeCombo_->addItem("读写 rw", static_cast<int>(vfs::OpenMode::ReadWrite));
    modeCombo_->addItem("追加 a", static_cast<int>(vfs::OpenMode::Append));

    fdList_ = new QListWidget;
    contentEdit_ = new QTextEdit;
    contentEdit_->setPlaceholderText("文件内容会显示在这里，也可以在这里输入要写入的内容。");
    logEdit_ = new QTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(130);

    auto* pathBar = new QWidget;
    auto* pathLayout = new QHBoxLayout(pathBar);
    pathLayout->addWidget(userLabel_);
    pathLayout->addWidget(pathLabel_, 1);
    pathLayout->addWidget(pathEdit_);
    pathLayout->addWidget(cdButton);

    auto* filePanel = new QWidget;
    auto* fileLayout = new QVBoxLayout(filePanel);
    fileLayout->addWidget(pathBar);
    fileLayout->addWidget(fileTable_);

    auto* operationPanel = new QWidget;
    auto* operationLayout = new QVBoxLayout(operationPanel);
    operationLayout->addWidget(new QLabel("打开模式"));
    operationLayout->addWidget(modeCombo_);
    operationLayout->addWidget(new QLabel("打开文件 fd"));
    operationLayout->addWidget(fdList_);
    operationLayout->addWidget(new QLabel("文件内容"));
    operationLayout->addWidget(contentEdit_, 1);
    operationLayout->addWidget(logEdit_);

    auto* splitter = new QSplitter;
    splitter->addWidget(loginPanel);
    splitter->addWidget(filePanel);
    splitter->addWidget(operationPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 1);

    setCentralWidget(splitter);
    statusBar()->showMessage("就绪");

    connect(formatAction, &QAction::triggered, this, [this] { formatVolume(); });
    connect(refreshAction, &QAction::triggered, this, [this] { refreshDirectory(); });
    connect(exitAction, &QAction::triggered, this, [this] { close(); });
    connect(createFileAction, &QAction::triggered, this, [this] { createFile(); });
    connect(createDirAction, &QAction::triggered, this, [this] { createDirectory(); });
    connect(deleteAction, &QAction::triggered, this, [this] { deleteSelected(); });
    connect(openAction, &QAction::triggered, this, [this] { openSelected(); });
    connect(readAction, &QAction::triggered, this, [this] { readOpenedFile(); });
    connect(writeAction, &QAction::triggered, this, [this] { writeOpenedFile(); });
    connect(closeAction, &QAction::triggered, this, [this] { closeSelectedFd(); });
    connect(loginButton, &QPushButton::clicked, this, [this] { login(); });
    connect(logoutButton, &QPushButton::clicked, this, [this] { logout(); });
    connect(cdButton, &QPushButton::clicked, this, [this] { changeDirectory(); });
    connect(fileTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openSelected(); });
}

void MainWindow::connectActions() {}

void MainWindow::refreshState() {
    userLabel_->setText(fs_.isLoggedIn() ? "当前用户: " + toQString(fs_.currentUserName()) : "当前用户: 未登录");
    pathLabel_->setText("当前目录: " + toQString(fs_.currentDirectoryPath()));
    refreshDirectory();
}

void MainWindow::refreshDirectory(const QString& path) {
    fileTable_->setRowCount(0);
    std::string message;
    const std::vector<vfs::DirectoryViewItem> items = fs_.listDirectoryAt(toStdString(path), message);
    for (const auto& item : items) {
        const int row = fileTable_->rowCount();
        fileTable_->insertRow(row);
        fileTable_->setItem(row, 0, new QTableWidgetItem(toQString(item.name)));
        fileTable_->setItem(row, 1, new QTableWidgetItem(typeName(item.type)));
        fileTable_->setItem(row, 2, new QTableWidgetItem(QString::number(item.inodeNo)));
        fileTable_->setItem(row, 3, new QTableWidgetItem(QString::number(item.size)));
        fileTable_->setItem(row, 4, new QTableWidgetItem(permissionText(item.permissions)));
    }
    pathLabel_->setText("当前目录: " + toQString(fs_.currentDirectoryPath()));
    statusBar()->showMessage(toQString(message));
}

void MainWindow::appendLog(const QString& text) {
    logEdit_->append(text);
    statusBar()->showMessage(text);
}

void MainWindow::login() {
    std::string message;
    fs_.loginUser(toStdString(userEdit_->text()), toStdString(passwordEdit_->text()), message);
    appendLog(toQString(message));
    refreshState();
}

void MainWindow::logout() {
    std::string message;
    fs_.logoutUser(message);
    fdList_->clear();
    contentEdit_->clear();
    appendLog(toQString(message));
    refreshState();
}

void MainWindow::formatVolume() {
    if (QMessageBox::question(this, "格式化", "格式化会清空虚拟磁盘，确定继续吗？") != QMessageBox::Yes) {
        return;
    }
    fs_.format();
    fdList_->clear();
    contentEdit_->clear();
    appendLog("文件系统已格式化。");
    refreshState();
}

void MainWindow::createFile() {
    const QString name = QInputDialog::getText(this, "新建文件", "文件名或路径:");
    if (name.isEmpty()) {
        return;
    }
    std::string message;
    fs_.createFileAt(toStdString(name), message);
    appendLog(toQString(message));
    refreshDirectory();
}

void MainWindow::createDirectory() {
    const QString name = QInputDialog::getText(this, "新建文件夹", "文件夹名或路径:");
    if (name.isEmpty()) {
        return;
    }
    std::string message;
    fs_.createDirectoryAt(toStdString(name), message);
    appendLog(toQString(message));
    refreshDirectory();
}

void MainWindow::deleteSelected() {
    const QString name = selectedEntryName();
    if (name.isEmpty()) {
        appendLog("请先选择一个文件或文件夹。");
        return;
    }
    std::string message;
    fs_.deleteAt(toStdString(name), message);
    appendLog(toQString(message));
    refreshDirectory();
}

void MainWindow::changeDirectory() {
    std::string message;
    fs_.changeDirectoryTo(toStdString(pathEdit_->text()), message);
    appendLog(toQString(message));
    refreshState();
}

void MainWindow::openSelected() {
    const QString name = selectedEntryName();
    if (name.isEmpty()) {
        appendLog("请先选择一个文件。");
        return;
    }
    int fd = -1;
    std::string message;
    if (fs_.openFileAt(toStdString(name), selectedOpenMode(), fd, message)) {
        fdList_->addItem(QString("fd %1 - %2").arg(fd).arg(name));
    }
    appendLog(toQString(message));
}

void MainWindow::closeSelectedFd() {
    const int fd = selectedFd();
    std::string message;
    fs_.closeDescriptor(fd, message);
    delete fdList_->takeItem(fdList_->currentRow());
    appendLog(toQString(message));
}

void MainWindow::readOpenedFile() {
    std::string content;
    std::string message;
    fs_.readDescriptor(selectedFd(), content, message);
    contentEdit_->setPlainText(toQString(content));
    appendLog(toQString(message));
}

void MainWindow::writeOpenedFile() {
    std::string message;
    fs_.writeDescriptor(selectedFd(), toStdString(contentEdit_->toPlainText()), message);
    appendLog(toQString(message));
    refreshDirectory();
}

QString MainWindow::selectedEntryName() const {
    const QModelIndexList rows = fileTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return QString();
    }
    return fileTable_->item(rows.first().row(), 0)->text();
}

int MainWindow::selectedFd() const {
    QListWidgetItem* item = fdList_->currentItem();
    if (!item) {
        return -1;
    }
    const QString text = item->text();
    const QString number = text.section(' ', 1, 1);
    return number.toInt();
}

vfs::OpenMode MainWindow::selectedOpenMode() const {
    return static_cast<vfs::OpenMode>(modeCombo_->currentData().toInt());
}
