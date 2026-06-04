#ifndef QT_MAINWINDOW_H
#define QT_MAINWINDOW_H

#include "vfs.h"

#include <QMainWindow>
#include <QMap>
#include <QListWidget>
#include <QStringList>

class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTextEdit;

class FileListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FileListWidget(QWidget* parent = nullptr);
    void setDirectoryPath(const QString& path);
    QString directoryPath() const;
signals:
    void fileDroppedOnDirectory(const QString& sourceName, const QString& targetDirName);
protected:
    void dropEvent(QDropEvent* event) override;
private:
    QString directoryPath_;
};

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void refreshState();
    void refreshDesktop();
    void populateIconList(QListWidget* list, const QString& directoryPath);
    void appendLog(const QString& text);
    bool requireLogin();
    bool isAtUserHome() const;
    void updatePathBar();

    void login();
    void logout();
    void registerUser();
    void showRegisterPage();
    void showLoginPage();
    void formatVolume();
    void showSystemMenu(const QPoint& pos);
    void showBlockAllocationDialog();
    void showUserStorageDialog();
    void navigateBack();
    void navigateToPath();

    void showIconMenu(QListWidget* list, const QString& directoryPath, const QPoint& pos);
    void createFileIn(const QString& directoryPath);
    void createDirectoryIn(const QString& directoryPath);
    void deleteEntryIn(const QString& directoryPath, QListWidgetItem* item);
    void moveEntryIn(const QString& sourceName, const QString& targetDirName);
    void openEntryIn(const QString& directoryPath, QListWidgetItem* item);
    void openDirectoryWindow(const QString& path, const QString& title);
    void openFileWindow(const QString& path, const QString& title, std::uint16_t permissions);

    QString desktopPath() const;
    QString childPath(const QString& directoryPath, const QString& name) const;
    QString itemName(QListWidgetItem* item) const;
    vfs::InodeType itemType(QListWidgetItem* item) const;
    std::uint16_t itemPermissions(QListWidgetItem* item) const;
    QString permissionSummary(std::uint16_t permissions) const;

    vfs::FileSystem fs_;
    QStackedWidget* pages_ = nullptr;
    QWidget* loginPage_ = nullptr;
    QWidget* registerPage_ = nullptr;
    QWidget* mainPage_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* regUserEdit_ = nullptr;
    QLineEdit* regPasswordEdit_ = nullptr;
    QLineEdit* regConfirmEdit_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QLabel* userLabel_ = nullptr;
    QLabel* systemIcon_ = nullptr;
    FileListWidget* desktopList_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
    QMap<QString, QStringList> directoryOrder_;
};

#endif
