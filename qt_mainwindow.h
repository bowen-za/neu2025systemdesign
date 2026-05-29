#ifndef QT_MAINWINDOW_H
#define QT_MAINWINDOW_H

#include "vfs.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTextEdit;

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

    void login();
    void logout();
    void formatVolume();
    void showSystemMenu(const QPoint& pos);

    void showIconMenu(QListWidget* list, const QString& directoryPath, const QPoint& pos);
    void createFileIn(const QString& directoryPath);
    void createDirectoryIn(const QString& directoryPath);
    void deleteEntryIn(const QString& directoryPath, QListWidgetItem* item);
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
    QWidget* mainPage_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLabel* userLabel_ = nullptr;
    QLabel* systemIcon_ = nullptr;
    QListWidget* desktopList_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
};

#endif
