#include "qt_mainwindow.h"

#include <algorithm>
#include <limits>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

constexpr const char* kSystemName = "BYSX OS";
constexpr const char* kLoginSubtitle = "Virtual UNIX File System";

QString toQString(const std::string& value) {
    return QString::fromUtf8(value.c_str());
}

std::string toStdString(const QString& value) {
    return value.toUtf8().constData();
}

QString typeName(vfs::InodeType type) {
    return type == vfs::InodeType::Directory ? QStringLiteral("目录") : QStringLiteral("文件");
}

QPushButton* makeButton(const QString& text, const QIcon& icon = QIcon()) {
    auto* button = new QPushButton(icon, text);
    button->setMinimumHeight(34);
    return button;
}

QIcon entryIcon(vfs::InodeType type) {
    const QStyle* style = QApplication::style();
    return type == vfs::InodeType::Directory
        ? style->standardIcon(QStyle::SP_DirIcon)
        : style->standardIcon(QStyle::SP_FileIcon);
}

bool isSpecialEntry(const QString& name) {
    return name == QStringLiteral(".") || name == QStringLiteral("..");
}

}  // namespace

FileListWidget::FileListWidget(QWidget* parent) : QListWidget(parent) {
}

void FileListWidget::setDirectoryPath(const QString& path) {
    directoryPath_ = path;
}

QString FileListWidget::directoryPath() const {
    return directoryPath_;
}

void FileListWidget::dropEvent(QDropEvent* event) {
    const QPoint dropPos = event->position().toPoint();
    QListWidgetItem* targetItem = itemAt(dropPos);

    if (targetItem) {
        const vfs::InodeType targetType = static_cast<vfs::InodeType>(
            targetItem->data(Qt::UserRole + 1).toInt());

        const QList<QListWidgetItem*> selected = selectedItems();
        for (auto* item : selected) {
            if (item == targetItem) {
                continue;
            }
            const vfs::InodeType type = static_cast<vfs::InodeType>(
                item->data(Qt::UserRole + 1).toInt());
            if (type == vfs::InodeType::File && targetType == vfs::InodeType::Directory) {
                emit fileDroppedOnDirectory(
                    item->data(Qt::UserRole).toString(),
                    targetItem->data(Qt::UserRole).toString());
                event->acceptProposedAction();
                return;
            }
        }
    }

    QListWidget::dropEvent(event);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    fs_.initialize();
    buildUi();
    refreshState();
}

void MainWindow::buildUi() {
    setWindowTitle(QString::fromUtf8(kSystemName));
    setWindowIcon(QIcon(QStringLiteral(":/assets/assets/bysx_os_icon.png")));
    resize(1100, 720);

    pages_ = new QStackedWidget(this);

    loginPage_ = new QWidget;
    loginPage_->setObjectName(QStringLiteral("loginPage"));
    auto* loginRoot = new QVBoxLayout(loginPage_);
    loginRoot->setContentsMargins(32, 32, 32, 24);

    auto* loginCenter = new QWidget;
    auto* loginCenterLayout = new QHBoxLayout(loginCenter);
    loginCenterLayout->addStretch();

    auto* loginBox = new QFrame;
    loginBox->setObjectName(QStringLiteral("loginBox"));
    loginBox->setFrameShape(QFrame::StyledPanel);
    loginBox->setMinimumWidth(380);
    loginBox->setMaximumWidth(430);

    auto* loginLayout = new QVBoxLayout(loginBox);
    loginLayout->setContentsMargins(30, 28, 30, 28);
    loginLayout->setSpacing(12);

    auto* iconLabel = new QLabel;
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(QPixmap(QStringLiteral(":/assets/assets/bysx_os_icon.png")).scaled(88, 88, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto* title = new QLabel(QString::fromUtf8(kSystemName));
    QFont titleFont = title->font();
    titleFont.setPointSize(17);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);

    auto* subtitle = new QLabel(QString::fromUtf8(kLoginSubtitle));
    subtitle->setAlignment(Qt::AlignCenter);

    userEdit_ = new QLineEdit(QStringLiteral("usr1"));
    userEdit_->setPlaceholderText(QStringLiteral("用户名"));
    userEdit_->setMinimumHeight(34);

    passwordEdit_ = new QLineEdit(QStringLiteral("pass1"));
    passwordEdit_->setPlaceholderText(QStringLiteral("密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setMinimumHeight(34);

    auto* loginButton = makeButton(QStringLiteral("登录"), QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton));
    loginButton->setDefault(true);

    loginLayout->addWidget(iconLabel);
    loginLayout->addWidget(title);
    loginLayout->addWidget(subtitle);
    loginLayout->addSpacing(8);
    loginLayout->addWidget(new QLabel(QStringLiteral("用户名")));
    loginLayout->addWidget(userEdit_);
    loginLayout->addWidget(new QLabel(QStringLiteral("密码")));
    loginLayout->addWidget(passwordEdit_);
    loginLayout->addSpacing(8);
    loginLayout->addWidget(loginButton);

    loginCenterLayout->addWidget(loginBox);
    loginCenterLayout->addStretch();

    auto* loginBottom = new QWidget;
    auto* loginBottomLayout = new QHBoxLayout(loginBottom);
    loginBottomLayout->setContentsMargins(0, 0, 0, 0);
    loginBottomLayout->addStretch();
    auto* formatButton = makeButton(QStringLiteral("格式化"), QApplication::style()->standardIcon(QStyle::SP_DriveHDIcon));
    auto* registerButton = makeButton(QStringLiteral("注册"), QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    auto* exitLoginButton = makeButton(QStringLiteral("退出"), QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));
    loginBottomLayout->addWidget(formatButton);
    loginBottomLayout->addWidget(registerButton);
    loginBottomLayout->addWidget(exitLoginButton);

    loginRoot->addStretch();
    loginRoot->addWidget(loginCenter);
    loginRoot->addStretch();
    loginRoot->addWidget(loginBottom);

    registerPage_ = new QWidget;
    registerPage_->setObjectName(QStringLiteral("registerPage"));
    auto* regRoot = new QVBoxLayout(registerPage_);
    regRoot->setContentsMargins(32, 32, 32, 24);

    auto* regCenter = new QWidget;
    auto* regCenterLayout = new QHBoxLayout(regCenter);
    regCenterLayout->addStretch();

    auto* regBox = new QFrame;
    regBox->setObjectName(QStringLiteral("loginBox"));
    regBox->setFrameShape(QFrame::StyledPanel);
    regBox->setMinimumWidth(380);
    regBox->setMaximumWidth(430);

    auto* regLayout = new QVBoxLayout(regBox);
    regLayout->setContentsMargins(30, 28, 30, 28);
    regLayout->setSpacing(12);

    auto* regIcon = new QLabel;
    regIcon->setAlignment(Qt::AlignCenter);
    regIcon->setPixmap(QPixmap(QStringLiteral(":/assets/assets/bysx_os_icon.png")).scaled(88, 88, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto* regTitle = new QLabel(QStringLiteral("注册新用户"));
    QFont regTitleFont = regTitle->font();
    regTitleFont.setPointSize(17);
    regTitleFont.setBold(true);
    regTitle->setFont(regTitleFont);
    regTitle->setAlignment(Qt::AlignCenter);

    auto* regSubtitle = new QLabel(QString::fromUtf8(kLoginSubtitle));
    regSubtitle->setAlignment(Qt::AlignCenter);

    regUserEdit_ = new QLineEdit;
    regUserEdit_->setPlaceholderText(QStringLiteral("用户名"));
    regUserEdit_->setMinimumHeight(34);

    regPasswordEdit_ = new QLineEdit;
    regPasswordEdit_->setPlaceholderText(QStringLiteral("密码"));
    regPasswordEdit_->setEchoMode(QLineEdit::Password);
    regPasswordEdit_->setMinimumHeight(34);

    regConfirmEdit_ = new QLineEdit;
    regConfirmEdit_->setPlaceholderText(QStringLiteral("确认密码"));
    regConfirmEdit_->setEchoMode(QLineEdit::Password);
    regConfirmEdit_->setMinimumHeight(34);

    auto* regSubmitButton = makeButton(QStringLiteral("注册"), QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton));
    regSubmitButton->setDefault(true);

    regLayout->addWidget(regIcon);
    regLayout->addWidget(regTitle);
    regLayout->addWidget(regSubtitle);
    regLayout->addSpacing(8);
    regLayout->addWidget(new QLabel(QStringLiteral("用户名")));
    regLayout->addWidget(regUserEdit_);
    regLayout->addWidget(new QLabel(QStringLiteral("密码")));
    regLayout->addWidget(regPasswordEdit_);
    regLayout->addWidget(new QLabel(QStringLiteral("确认密码")));
    regLayout->addWidget(regConfirmEdit_);
    regLayout->addSpacing(8);
    regLayout->addWidget(regSubmitButton);

    regCenterLayout->addWidget(regBox);
    regCenterLayout->addStretch();

    auto* regBottom = new QWidget;
    auto* regBottomLayout = new QHBoxLayout(regBottom);
    regBottomLayout->setContentsMargins(0, 0, 0, 0);
    regBottomLayout->addStretch();
    auto* backToLoginButton = makeButton(QStringLiteral("返回登录"), QApplication::style()->standardIcon(QStyle::SP_ArrowBack));
    regBottomLayout->addWidget(backToLoginButton);

    regRoot->addStretch();
    regRoot->addWidget(regCenter);
    regRoot->addStretch();
    regRoot->addWidget(regBottom);

    mainPage_ = new QWidget;
    mainPage_->setObjectName(QStringLiteral("mainPage"));
    auto* mainRoot = new QVBoxLayout(mainPage_);
    mainRoot->setContentsMargins(14, 12, 14, 12);
    mainRoot->setSpacing(8);

    auto* desktopHeader = new QWidget;
    auto* headerLayout = new QHBoxLayout(desktopHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    backButton_ = new QPushButton(QStringLiteral("\u2190"));
    backButton_->setObjectName(QStringLiteral("backButton"));
    backButton_->setFixedSize(34, 34);
    backButton_->setToolTip(QStringLiteral("返回上级目录"));

    pathEdit_ = new QLineEdit;
    pathEdit_->setObjectName(QStringLiteral("pathEdit"));
    pathEdit_->setMinimumHeight(32);

    userLabel_ = new QLabel;
    userLabel_->setObjectName(QStringLiteral("userLabel"));

    headerLayout->addWidget(backButton_);
    headerLayout->addWidget(pathEdit_, 1);
    headerLayout->addWidget(userLabel_);

    desktopList_ = new FileListWidget;
    desktopList_->setObjectName(QStringLiteral("desktopList"));
    desktopList_->setViewMode(QListView::IconMode);
    desktopList_->setIconSize(QSize(48, 48));
    desktopList_->setGridSize(QSize(112, 92));
    desktopList_->setResizeMode(QListView::Adjust);
    desktopList_->setMovement(QListView::Static);
    desktopList_->setSelectionMode(QAbstractItemView::SingleSelection);
    desktopList_->setContextMenuPolicy(Qt::CustomContextMenu);
    desktopList_->setWordWrap(true);
    desktopList_->setDragDropMode(QAbstractItemView::InternalMove);
    desktopList_->setDefaultDropAction(Qt::MoveAction);

    auto* bottomBar = new QWidget;
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addStretch();
    systemIcon_ = new QLabel;
    systemIcon_->setObjectName(QStringLiteral("systemIcon"));
    systemIcon_->setAlignment(Qt::AlignCenter);
    systemIcon_->setContextMenuPolicy(Qt::CustomContextMenu);
    systemIcon_->setToolTip(QStringLiteral("右键打开系统菜单"));
    systemIcon_->setPixmap(QPixmap(QStringLiteral(":/assets/assets/bysx_os_icon.png")).scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    systemIcon_->setFixedSize(76, 76);
    bottomLayout->addWidget(systemIcon_);
    bottomLayout->addStretch();

    mainRoot->addWidget(desktopHeader);
    mainRoot->addWidget(desktopList_, 1);
    mainRoot->addWidget(bottomBar);

    pages_->addWidget(loginPage_);
    pages_->addWidget(registerPage_);
    pages_->addWidget(mainPage_);
    setCentralWidget(pages_);
    setStyleSheet(QStringLiteral(
        "#loginPage, #registerPage, #mainPage {"
        "  border-image: url(:/assets/assets/bysx_os_wallpaper.png) 0 0 0 0 stretch stretch;"
        "}"
        "#loginBox {"
        "  background: rgba(255, 255, 255, 218);"
        "  border: 1px solid rgba(255, 255, 255, 170);"
        "  border-radius: 10px;"
        "}"
        "#userLabel {"
        "  color: white;"
        "  background: rgba(0, 35, 70, 120);"
        "  padding: 6px 10px;"
        "  border-radius: 6px;"
        "}"
        "#backButton {"
        "  color: white;"
        "  background: rgba(0, 35, 70, 140);"
        "  border: 1px solid rgba(255, 255, 255, 80);"
        "  border-radius: 6px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "}"
        "#backButton:hover {"
        "  background: rgba(44, 160, 255, 160);"
        "}"
        "#pathEdit {"
        "  color: white;"
        "  background: rgba(0, 26, 58, 140);"
        "  border: 1px solid rgba(255, 255, 255, 80);"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "#systemIcon {"
        "  background: rgba(255, 255, 255, 65);"
        "  border: 1px solid rgba(255, 255, 255, 130);"
        "  border-radius: 12px;"
        "}"
        "#desktopList {"
        "  background: rgba(0, 26, 58, 70);"
        "  border: 1px solid rgba(255, 255, 255, 60);"
        "  color: white;"
        "  outline: none;"
        "}"
        "#desktopList::item {"
        "  padding: 8px;"
        "  color: white;"
        "}"
        "#desktopList::item:selected {"
        "  background: rgba(44, 160, 255, 150);"
        "  border-radius: 6px;"
        "}"
    ));
    statusBar()->showMessage(QStringLiteral("请先登录"));

    connect(loginButton, &QPushButton::clicked, this, [this] { login(); });
    connect(passwordEdit_, &QLineEdit::returnPressed, this, [this] { login(); });
    connect(formatButton, &QPushButton::clicked, this, [this] { formatVolume(); });
    connect(registerButton, &QPushButton::clicked, this, [this] { showRegisterPage(); });
    connect(exitLoginButton, &QPushButton::clicked, this, [this] { close(); });
    connect(regSubmitButton, &QPushButton::clicked, this, [this] { registerUser(); });
    connect(regConfirmEdit_, &QLineEdit::returnPressed, this, [this] { registerUser(); });
    connect(backToLoginButton, &QPushButton::clicked, this, [this] { showLoginPage(); });
    connect(systemIcon_, &QLabel::customContextMenuRequested, this, [this](const QPoint& pos) {
        showSystemMenu(pos);
    });
    connect(desktopList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIconMenu(desktopList_, desktopPath(), pos);
    });
    connect(desktopList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!requireLogin()) {
            return;
        }
        if (itemType(item) == vfs::InodeType::Directory) {
            const QString name = itemName(item);
            const QString path = childPath(desktopPath(), name);
            std::string message;
            if (fs_.changeDirectoryTo(toStdString(path), message)) {
                appendLog(toQString(message));
                refreshDesktop();
            } else {
                QMessageBox::warning(this, QStringLiteral("导航失败"), toQString(message));
                appendLog(toQString(message));
            }
        } else {
            openEntryIn(desktopPath(), item);
        }
    });
    connect(backButton_, &QPushButton::clicked, this, [this] { navigateBack(); });
    connect(pathEdit_, &QLineEdit::returnPressed, this, [this] { navigateToPath(); });
    connect(desktopList_, &FileListWidget::fileDroppedOnDirectory, this, [this](const QString& source, const QString& target) {
        moveEntryIn(source, target);
    });
}

void MainWindow::refreshState() {
    const bool loggedIn = fs_.isLoggedIn();
    pages_->setCurrentWidget(loggedIn ? mainPage_ : loginPage_);

    if (loggedIn) {
        userLabel_->setText(QStringLiteral("当前用户: ") + toQString(fs_.currentUserName()));
        refreshDesktop();
        statusBar()->showMessage(QStringLiteral("已登录"));
    } else {
        desktopList_->clear();
        if (logEdit_) {
            logEdit_->clear();
        }
        statusBar()->showMessage(QStringLiteral("请先登录"));
    }
}

void MainWindow::refreshDesktop() {
    updatePathBar();
    populateIconList(desktopList_, desktopPath());
}

void MainWindow::populateIconList(QListWidget* list, const QString& directoryPath) {
    if (auto* fileList = qobject_cast<FileListWidget*>(list)) {
        fileList->setDirectoryPath(directoryPath);
    }
    if (list->count() > 0) {
        QStringList currentOrder;
        for (int i = 0; i < list->count(); ++i) {
            currentOrder.append(itemName(list->item(i)));
        }
        directoryOrder_[directoryPath] = currentOrder;
    }

    list->clear();

    std::string message;
    const auto items = fs_.listDirectoryAt(toStdString(directoryPath), message);

    struct EntryData {
        QString name;
        vfs::InodeType type;
        std::uint32_t inodeNo;
        std::uint64_t size;
        std::uint16_t permissions;
    };

    QList<EntryData> entries;
    for (const auto& item : items) {
        const QString name = toQString(item.name);
        if (isSpecialEntry(name)) {
            continue;
        }
        entries.append({name, item.type, item.inodeNo, item.size, item.permissions});
    }

    const QStringList savedOrder = directoryOrder_.value(directoryPath);
    if (!savedOrder.isEmpty()) {
        QMap<QString, int> orderIndex;
        for (int i = 0; i < savedOrder.size(); ++i) {
            orderIndex[savedOrder[i]] = i;
        }

        std::sort(entries.begin(), entries.end(), [&orderIndex](const EntryData& a, const EntryData& b) {
            const int ai = orderIndex.value(a.name, std::numeric_limits<int>::max());
            const int bi = orderIndex.value(b.name, std::numeric_limits<int>::max());
            if (ai != bi) {
                return ai < bi;
            }
            return a.name < b.name;
        });
    }

    for (const auto& entry : entries) {
        auto* widgetItem = new QListWidgetItem(entryIcon(entry.type), entry.name);
        widgetItem->setData(Qt::UserRole, entry.name);
        widgetItem->setData(Qt::UserRole + 1, static_cast<int>(entry.type));
        widgetItem->setData(Qt::UserRole + 2, static_cast<int>(entry.permissions));
        widgetItem->setToolTip(QStringLiteral("%1\ninode: %2\n大小: %3\n权限: %4 %5")
            .arg(typeName(entry.type))
            .arg(entry.inodeNo)
            .arg(entry.size)
            .arg(QString::number(entry.permissions, 8))
            .arg(permissionSummary(entry.permissions)));
        list->addItem(widgetItem);
    }

    statusBar()->showMessage(toQString(message));
}

void MainWindow::appendLog(const QString& text) {
    if (logEdit_) {
        logEdit_->append(text);
    }
    statusBar()->showMessage(text);
}

bool MainWindow::requireLogin() {
    if (fs_.isLoggedIn()) {
        return true;
    }
    QMessageBox::information(this, QStringLiteral("需要登录"), QStringLiteral("请先登录用户后再使用文件系统功能。"));
    refreshState();
    return false;
}

void MainWindow::login() {
    std::string message;
    if (fs_.loginUser(toStdString(userEdit_->text()), toStdString(passwordEdit_->text()), message)) {
        refreshState();
        appendLog(toQString(message));
        return;
    }
    QMessageBox::warning(this, QStringLiteral("登录失败"), toQString(message));
    statusBar()->showMessage(toQString(message));
}

void MainWindow::logout() {
    std::string message;
    fs_.logoutUser(message);
    appendLog(toQString(message));
    refreshState();
}

void MainWindow::registerUser() {
    const QString username = regUserEdit_->text().trimmed();
    const QString password = regPasswordEdit_->text();
    const QString confirm = regConfirmEdit_->text();

    if (username.isEmpty() || password.isEmpty() || confirm.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("注册失败"), QStringLiteral("所有字段均不能为空。"));
        statusBar()->showMessage(QStringLiteral("注册失败：所有字段均不能为空。"));
        return;
    }

    if (password != confirm) {
        QMessageBox::warning(this, QStringLiteral("注册失败"), QStringLiteral("两次输入的密码不一致。"));
        statusBar()->showMessage(QStringLiteral("注册失败：两次输入的密码不一致。"));
        return;
    }

    std::string message;
    if (fs_.registerUser(toStdString(username), toStdString(password), message)) {
        QMessageBox::information(this, QStringLiteral("注册成功"), toQString(message));
        statusBar()->showMessage(toQString(message));
        regUserEdit_->clear();
        regPasswordEdit_->clear();
        regConfirmEdit_->clear();
        showLoginPage();
        userEdit_->setText(username);
    } else {
        QMessageBox::warning(this, QStringLiteral("注册失败"), toQString(message));
        statusBar()->showMessage(toQString(message));
    }
}

void MainWindow::showRegisterPage() {
    pages_->setCurrentWidget(registerPage_);
    regUserEdit_->setFocus();
}

void MainWindow::showLoginPage() {
    pages_->setCurrentWidget(loginPage_);
    userEdit_->setFocus();
}

void MainWindow::formatVolume() {
    if (QMessageBox::question(this, QStringLiteral("格式化"), QStringLiteral("格式化会清空虚拟磁盘并重建默认用户，确定继续吗？")) != QMessageBox::Yes) {
        return;
    }
    fs_.format();
    appendLog(QStringLiteral("文件系统已格式化。"));
    refreshState();
}

void MainWindow::showSystemMenu(const QPoint& pos) {
    QMenu menu(this);
    QAction* formatAction = menu.addAction(QStringLiteral("格式化"));
    menu.addSeparator();
    QAction* logoutAction = menu.addAction(QStringLiteral("注销"));
    QAction* exitAction = menu.addAction(QStringLiteral("退出"));

    QAction* chosen = menu.exec(systemIcon_->mapToGlobal(pos));
    if (chosen == formatAction) {
        formatVolume();
    } else if (chosen == logoutAction) {
        logout();
    } else if (chosen == exitAction) {
        close();
    }
}

void MainWindow::showIconMenu(QListWidget* list, const QString& directoryPath, const QPoint& pos) {
    if (!requireLogin()) {
        return;
    }

    QListWidgetItem* clickedItem = list->itemAt(pos);
    QMenu menu(this);

    if (clickedItem) {
        QAction* openAction = menu.addAction(QStringLiteral("打开"));
        QAction* deleteAction = menu.addAction(QStringLiteral("删除"));
        menu.addSeparator();
        QAction* refreshAction = menu.addAction(QStringLiteral("刷新"));

        QAction* chosen = menu.exec(list->viewport()->mapToGlobal(pos));
        if (chosen == openAction) {
            if (list == desktopList_ && itemType(clickedItem) == vfs::InodeType::Directory) {
                const QString name = itemName(clickedItem);
                const QString path = childPath(directoryPath, name);
                std::string message;
                if (fs_.changeDirectoryTo(toStdString(path), message)) {
                    appendLog(toQString(message));
                    refreshDesktop();
                } else {
                    QMessageBox::warning(this, QStringLiteral("导航失败"), toQString(message));
                    appendLog(toQString(message));
                }
            } else {
                openEntryIn(directoryPath, clickedItem);
            }
        } else if (chosen == deleteAction) {
            deleteEntryIn(directoryPath, clickedItem);
        } else if (chosen == refreshAction) {
            populateIconList(list, directoryPath);
        }
        return;
    }

    QAction* newFileAction = menu.addAction(QStringLiteral("新建文件"));
    QAction* newDirAction = menu.addAction(QStringLiteral("新建目录"));
    menu.addSeparator();
    QAction* refreshAction = menu.addAction(QStringLiteral("刷新"));

    QAction* chosen = menu.exec(list->viewport()->mapToGlobal(pos));
    if (chosen == newFileAction) {
        createFileIn(directoryPath);
    } else if (chosen == newDirAction) {
        createDirectoryIn(directoryPath);
    } else if (chosen == refreshAction) {
        populateIconList(list, directoryPath);
    }
}

void MainWindow::createFileIn(const QString& directoryPath) {
    const QString name = QInputDialog::getText(this, QStringLiteral("新建文件"), QStringLiteral("文件名:")).trimmed();
    if (name.isEmpty()) {
        return;
    }
    if (!name.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, QStringLiteral("文件类型错误"), QStringLiteral("只能新建 .txt 文本文件，请输入以 .txt 结尾的文件名。"));
        return;
    }

    std::string message;
    fs_.createFileAt(toStdString(childPath(directoryPath, name)), message);
    appendLog(toQString(message));
    refreshDesktop();
}

void MainWindow::createDirectoryIn(const QString& directoryPath) {
    const QString name = QInputDialog::getText(this, QStringLiteral("新建目录"), QStringLiteral("目录名:")).trimmed();
    if (name.isEmpty()) {
        return;
    }

    std::string message;
    fs_.createDirectoryAt(toStdString(childPath(directoryPath, name)), message);
    appendLog(toQString(message));
    refreshDesktop();
}

void MainWindow::deleteEntryIn(const QString& directoryPath, QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString name = itemName(item);
    if (QMessageBox::question(this, QStringLiteral("删除"), QStringLiteral("确定删除“%1”吗？").arg(name)) != QMessageBox::Yes) {
        return;
    }

    std::string message;
    fs_.deleteAt(toStdString(childPath(directoryPath, name)), message);
    appendLog(toQString(message));
    refreshDesktop();
}

void MainWindow::moveEntryIn(const QString& sourceName, const QString& targetDirName) {
    if (!requireLogin()) {
        return;
    }

    const QString directoryPath = desktopPath();
    const QString sourcePath = childPath(directoryPath, sourceName);
    const QString destDirPath = childPath(directoryPath, targetDirName);

    std::string message;
    if (fs_.moveAt(toStdString(sourcePath), toStdString(destDirPath), message)) {
        directoryOrder_.remove(destDirPath);
        appendLog(toQString(message));
        refreshDesktop();
    } else {
        QMessageBox::warning(this, QStringLiteral("移动失败"), toQString(message));
        appendLog(toQString(message));
        refreshDesktop();
    }
}

void MainWindow::openEntryIn(const QString& directoryPath, QListWidgetItem* item) {
    if (!item || !requireLogin()) {
        return;
    }

    const QString name = itemName(item);
    const QString path = childPath(directoryPath, name);
    if (itemType(item) == vfs::InodeType::Directory) {
        openDirectoryWindow(path, name);
    } else {
        openFileWindow(path, name, itemPermissions(item));
    }
}

void MainWindow::openDirectoryWindow(const QString& path, const QString& title) {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("目录 - %1").arg(title));
    dialog->resize(620, 430);

    auto* layout = new QVBoxLayout(dialog);
    auto* info = new QLabel(QStringLiteral("路径: %1").arg(path));
    auto* list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(42, 42));
    list->setGridSize(QSize(104, 86));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setWordWrap(true);

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(info);
    layout->addWidget(list, 1);
    layout->addWidget(closeButtons);

    populateIconList(list, path);

    connect(closeButtons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(list, &QListWidget::customContextMenuRequested, this, [this, list, path](const QPoint& pos) {
        showIconMenu(list, path, pos);
        populateIconList(list, path);
        refreshDesktop();
    });
    connect(list, &QListWidget::itemDoubleClicked, this, [this, path](QListWidgetItem* item) {
        openEntryIn(path, item);
    });

    dialog->show();
}

void MainWindow::openFileWindow(const QString& path, const QString& title, std::uint16_t permissions) {
    QDialog modeDialog(this);
    modeDialog.setWindowTitle(QStringLiteral("打开文件 - %1").arg(title));

    auto* modeLayout = new QVBoxLayout(&modeDialog);
    modeLayout->addWidget(new QLabel(QStringLiteral("权限: %1 %2").arg(QString::number(permissions, 8)).arg(permissionSummary(permissions))));
    modeLayout->addWidget(new QLabel(QStringLiteral("打开方式")));

    auto* modeCombo = new QComboBox;
    modeCombo->addItem(QStringLiteral("只读 r"), static_cast<int>(vfs::OpenMode::Read));
    modeCombo->addItem(QStringLiteral("写入 w"), static_cast<int>(vfs::OpenMode::Write));
    modeCombo->addItem(QStringLiteral("读写 rw"), static_cast<int>(vfs::OpenMode::ReadWrite));
    modeCombo->addItem(QStringLiteral("追加 a"), static_cast<int>(vfs::OpenMode::Append));
    modeLayout->addWidget(modeCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    modeLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &modeDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &modeDialog, &QDialog::reject);

    if (modeDialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto mode = static_cast<vfs::OpenMode>(modeCombo->currentData().toInt());
    int fd = -1;
    std::string message;
    if (!fs_.openFileAt(toStdString(path), mode, fd, message)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), toQString(message));
        appendLog(toQString(message));
        return;
    }

    auto* fileDialog = new QDialog(this);
    fileDialog->setAttribute(Qt::WA_DeleteOnClose);
    fileDialog->setWindowTitle(QStringLiteral("文件 - %1").arg(title));
    fileDialog->resize(680, 500);

    auto* layout = new QVBoxLayout(fileDialog);
    layout->addWidget(new QLabel(QStringLiteral("路径: %1").arg(path)));
    layout->addWidget(new QLabel(QStringLiteral("权限: %1 %2").arg(QString::number(permissions, 8)).arg(permissionSummary(permissions))));

    auto* editor = new QTextEdit;
    layout->addWidget(editor, 1);

    auto* actionRow = new QHBoxLayout;
    auto* readButton = makeButton(QStringLiteral("读取"), QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    auto* writeButton = makeButton(QStringLiteral("写入"), QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* closeButton = makeButton(QStringLiteral("关闭"), QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));
    actionRow->addWidget(readButton);
    actionRow->addWidget(writeButton);
    actionRow->addStretch();
    actionRow->addWidget(closeButton);
    layout->addLayout(actionRow);

    auto closeDescriptor = [this, fd]() {
        std::string closeMessage;
        fs_.closeDescriptor(fd, closeMessage);
        appendLog(toQString(closeMessage));
    };

    connect(readButton, &QPushButton::clicked, this, [this, fd, editor] {
        std::string content;
        std::string readMessage;
        fs_.readDescriptor(fd, content, readMessage);
        editor->setPlainText(toQString(content));
        appendLog(toQString(readMessage));
    });
    connect(writeButton, &QPushButton::clicked, this, [this, fd, editor] {
        std::string writeMessage;
        fs_.writeDescriptor(fd, toStdString(editor->toPlainText()), writeMessage);
        appendLog(toQString(writeMessage));
        refreshDesktop();
    });
    connect(closeButton, &QPushButton::clicked, fileDialog, &QDialog::close);
    connect(fileDialog, &QDialog::finished, this, closeDescriptor);

    if (mode == vfs::OpenMode::Read || mode == vfs::OpenMode::ReadWrite) {
        std::string content;
        std::string readMessage;
        fs_.readDescriptor(fd, content, readMessage);
        editor->setPlainText(toQString(content));
        appendLog(toQString(readMessage));
    }

    appendLog(toQString(message));
    fileDialog->show();
}

QString MainWindow::desktopPath() const {
    return toQString(fs_.currentDirectoryPath());
}

QString MainWindow::childPath(const QString& directoryPath, const QString& name) const {
    if (directoryPath == QStringLiteral("/")) {
        return QStringLiteral("/") + name;
    }
    return directoryPath + QStringLiteral("/") + name;
}

QString MainWindow::itemName(QListWidgetItem* item) const {
    return item ? item->data(Qt::UserRole).toString() : QString();
}

vfs::InodeType MainWindow::itemType(QListWidgetItem* item) const {
    return item ? static_cast<vfs::InodeType>(item->data(Qt::UserRole + 1).toInt()) : vfs::InodeType::Free;
}

std::uint16_t MainWindow::itemPermissions(QListWidgetItem* item) const {
    return item ? static_cast<std::uint16_t>(item->data(Qt::UserRole + 2).toInt()) : 0;
}

QString MainWindow::permissionSummary(std::uint16_t permissions) const {
    QString text;
    const int groups[] = {
        static_cast<int>((permissions >> 6) & 7),
        static_cast<int>((permissions >> 3) & 7),
        static_cast<int>(permissions & 7),
    };

    for (int value : groups) {
        text += (value & 4) ? QStringLiteral("r") : QStringLiteral("-");
        text += (value & 2) ? QStringLiteral("w") : QStringLiteral("-");
        text += (value & 1) ? QStringLiteral("x") : QStringLiteral("-");
    }
    return QStringLiteral("(") + text + QStringLiteral(")");
}

bool MainWindow::isAtUserHome() const {
    const QString path = desktopPath();
    for (int i = 1; i <= static_cast<int>(vfs::kUserCount); ++i) {
        if (path == QStringLiteral("/usr%1").arg(i)) {
            return true;
        }
    }
    return false;
}

void MainWindow::updatePathBar() {
    if (!pathEdit_) {
        return;
    }
    const QString currentPath = desktopPath();
    pathEdit_->setText(currentPath);
    pathEdit_->setCursorPosition(0);
    if (backButton_) {
        backButton_->setVisible(!isAtUserHome());
    }
}

void MainWindow::navigateBack() {
    if (!requireLogin()) {
        return;
    }
    std::string message;
    if (!fs_.changeDirectoryTo("..", message)) {
        QMessageBox::warning(this, QStringLiteral("导航失败"), toQString(message));
        appendLog(toQString(message));
        return;
    }
    appendLog(toQString(message));
    refreshDesktop();
}

void MainWindow::navigateToPath() {
    if (!requireLogin()) {
        return;
    }
    const QString target = pathEdit_->text().trimmed();
    if (target.isEmpty()) {
        refreshDesktop();
        return;
    }
    std::string message;
    if (!fs_.changeDirectoryTo(toStdString(target), message)) {
        QMessageBox::warning(this, QStringLiteral("路径无效"), toQString(message));
        appendLog(toQString(message));
        pathEdit_->setText(desktopPath());
        return;
    }
    appendLog(toQString(message));
    refreshDesktop();
}
