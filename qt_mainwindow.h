#ifndef QT_MAINWINDOW_H
#define QT_MAINWINDOW_H

#include "vfs.h"

#include <QMainWindow>
#include <QModelIndex>

class QAction;
class QComboBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextEdit;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void connectActions();
    void refreshState();
    void refreshDirectory(const QString& path = QString());
    void appendLog(const QString& text);
    void login();
    void logout();
    void formatVolume();
    void createFile();
    void createDirectory();
    void deleteSelected();
    void changeDirectory();
    void openSelected();
    void closeSelectedFd();
    void readOpenedFile();
    void writeOpenedFile();
    QString selectedEntryName() const;
    int selectedFd() const;
    vfs::OpenMode selectedOpenMode() const;

    vfs::FileSystem fs_;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLabel* userLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QTableWidget* fileTable_ = nullptr;
    QListWidget* fdList_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QTextEdit* contentEdit_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
};

#endif
