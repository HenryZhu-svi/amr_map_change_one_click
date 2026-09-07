#pragma once

#include "core/Robot.h"
#include "network/RequestJob.h"

#include <QMainWindow>
#include <QQueue>

class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QJsonObject;
class QAction;
class QComboBox;
class QToolBar;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    enum Column { Selected, Name, Ip, Online, Task, CurrentMap, Md5, Result, ColumnCount };

    void buildUi();
    void retranslateUi();
    QString tx(const char *chinese, const char *english) const;
    void loadRobots();
    void saveRobots() const;
    void addRobot();
    void removeSelected();
    void refreshSelected();
    void downloadMap();
    void chooseAndUpload(bool switchAfterUpload);
    void startBatch(const QByteArray &mapBytes, const QString &mapName,
                    bool switchAfterUpload);
    void launchNext();
    void runRobotOperation(int row);
    void uploadRobot(int row);
    void verifyUpload(int row);
    void switchRobot(int row);
    void verifyCurrentMap(int row);
    void finishRobot(int row, const QString &result, bool success);
    void query(int row, quint16 port, quint16 command, const QByteArray &payload,
               std::function<void(RequestResult)> callback, int timeoutMs = 10000);
    Robot robotAt(int row) const;
    QList<int> selectedRows() const;
    void setCell(int row, Column column, const QString &text);
    void log(const QString &message);
    bool parseJson(const QByteArray &bytes, QJsonObject *object, QString *error) const;
    bool responseSucceeded(const QJsonObject &object, QString *error) const;

    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_batchLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QToolBar *m_toolbar = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QAction *m_addAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_downloadAction = nullptr;
    QAction *m_uploadAction = nullptr;
    QAction *m_uploadSwitchAction = nullptr;
    QQueue<int> m_pendingRows;
    int m_activeOperations = 0;
    int m_completedOperations = 0;
    int m_totalOperations = 0;
    QByteArray m_mapBytes;
    QString m_mapName;
    QString m_mapMd5;
    bool m_switchAfterUpload = false;
    bool m_english = false;
};
