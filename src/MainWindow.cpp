#include "MainWindow.h"

#include "protocol/RbkProtocol.h"

#include <QAction>
#include <QAbstractSocket>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int MaxConcurrentRobots = 3;

QString statusName(int status, bool english)
{
    if (status < 0) return english ? QStringLiteral("Unknown") : QStringLiteral("未知");
    switch (status) {
    case 0: return english ? QStringLiteral("Idle") : QStringLiteral("空闲");
    case 1: return english ? QStringLiteral("Waiting") : QStringLiteral("等待");
    case 2: return english ? QStringLiteral("Running") : QStringLiteral("运行中");
    case 3: return english ? QStringLiteral("Suspended") : QStringLiteral("已暂停");
    case 4: return english ? QStringLiteral("Completed") : QStringLiteral("已完成");
    case 5: return english ? QStringLiteral("Failed") : QStringLiteral("失败");
    case 6: return english ? QStringLiteral("Canceled") : QStringLiteral("已取消");
    default: return (english ? QStringLiteral("Unknown (%1)") : QStringLiteral("未知(%1)"))
                        .arg(status);
    }
}

QString knownResultText(const QString &text, bool english)
{
    static const QList<QPair<QString, QString>> values = {
        {QStringLiteral("等待操作"), QStringLiteral("Waiting")},
        {QStringLiteral("查询地图状态…"), QStringLiteral("Querying map status...")},
        {QStringLiteral("状态已刷新"), QStringLiteral("Status refreshed")},
        {QStringLiteral("下载当前地图…"), QStringLiteral("Downloading current map...")},
        {QStringLiteral("等待预检"), QStringLiteral("Waiting for precheck")},
        {QStringLiteral("检查导航状态…"), QStringLiteral("Checking navigation status...")},
        {QStringLiteral("上传地图…"), QStringLiteral("Uploading map...")},
        {QStringLiteral("校验机器人 MD5…"), QStringLiteral("Verifying robot MD5...")},
        {QStringLiteral("切换地图…"), QStringLiteral("Switching map...")},
        {QStringLiteral("验证当前地图…"), QStringLiteral("Verifying current map...")},
        {QStringLiteral("上传及 MD5 校验成功"), QStringLiteral("Upload and MD5 verification succeeded")},
        {QStringLiteral("上传、校验和切换成功"), QStringLiteral("Upload, verification and switch succeeded")},
        {QStringLiteral("当前地图验证不一致"), QStringLiteral("Current map verification mismatch")},
    };
    for (const auto &value : values) {
        if (text == value.first || text == value.second) return english ? value.second : value.first;
    }
    return text;
}

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_english = QSettings().value(QStringLiteral("language"), QStringLiteral("zh"))
                    .toString() == QStringLiteral("en");
    buildUi();
    loadRobots();
}

void MainWindow::buildUi()
{
    resize(1180, 720);

    m_toolbar = addToolBar(QString());
    m_toolbar->setMovable(false);
    m_logoLabel = new QLabel(m_toolbar);
    m_logoLabel->setFixedSize(58, 44);
    m_logoLabel->setContentsMargins(4, 2, 8, 2);
    m_logoLabel->setPixmap(QPixmap(QStringLiteral(":/branding/svi-logo-128.png"))
                               .scaled(46, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_toolbar->addWidget(m_logoLabel);
    setWindowIcon(QIcon(QStringLiteral(":/branding/svi-logo-128.png")));
    m_addAction = m_toolbar->addAction(QString(), this, &MainWindow::addRobot);
    m_removeAction = m_toolbar->addAction(QString(), this, &MainWindow::removeSelected);
    m_toolbar->addSeparator();
    m_refreshAction = m_toolbar->addAction(QString(), this, &MainWindow::refreshSelected);
    m_downloadAction = m_toolbar->addAction(QString(), this, &MainWindow::downloadMap);
    m_openMapAction = m_toolbar->addAction(QString(), this, &MainWindow::openMapFile);
    m_fitMapAction = m_toolbar->addAction(QString(), this, [this] { m_mapView->fitMap(); });
    m_toolbar->addSeparator();
    m_uploadAction = m_toolbar->addAction(QString(), this, [this] { chooseAndUpload(false); });
    m_uploadSwitchAction = m_toolbar->addAction(QString(), this,
                                                 [this] { chooseAndUpload(true); });
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_languageLabel = new QLabel(m_toolbar);
    m_toolbar->addWidget(m_languageLabel);
    m_languageCombo = new QComboBox(m_toolbar);
    m_languageCombo->addItem(QStringLiteral("中文"), QStringLiteral("zh"));
    m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_languageCombo->setCurrentIndex(m_english ? 1 : 0);
    m_toolbar->addWidget(m_languageCombo);
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_english = index == 1;
        QSettings().setValue(QStringLiteral("language"), m_english ? QStringLiteral("en")
                                                                  : QStringLiteral("zh"));
        retranslateUi();
    });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    m_table = new QTableWidget(0, ColumnCount, central);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(Result, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    m_batchLabel = new QLabel(central);
    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1500);

    m_tabs = new QTabWidget(central);
    auto *mapPage = new QWidget(m_tabs);
    auto *mapLayout = new QVBoxLayout(mapPage);
    mapLayout->setContentsMargins(4, 4, 4, 4);
    m_mapInfoLabel = new QLabel(mapPage);
    m_mapView = new MapView(mapPage);
    m_mapView->setMinimumHeight(260);
    mapLayout->addWidget(m_mapInfoLabel);
    mapLayout->addWidget(m_mapView, 1);
    m_tabs->addTab(mapPage, QString());
    m_tabs->addTab(m_log, QString());

    layout->addWidget(m_table, 3);
    layout->addWidget(m_batchLabel);
    layout->addWidget(m_tabs, 2);
    setCentralWidget(central);
    retranslateUi();
}

QString MainWindow::tx(const char *chinese, const char *english) const
{
    return QString::fromUtf8(m_english ? english : chinese);
}

QString MainWindow::localizedError(const QString &error) const
{
    if (!m_english) return error;
    if (error == QStringLiteral("协议头不足 16 字节")) return QStringLiteral("Protocol header is shorter than 16 bytes");
    if (error == QStringLiteral("协议魔数不是 5A 01")) return QStringLiteral("Protocol magic is not 5A 01");
    if (error == QStringLiteral("响应序列号不匹配")) return QStringLiteral("Response sequence does not match");
    if (error == QStringLiteral("响应编号不匹配")) return QStringLiteral("Response command does not match");
    if (error == QStringLiteral("连接或响应超时")) return QStringLiteral("Connection or response timed out");
    return error;
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tx("AMR 地图批量管理器 0.2", "AMR Map Manager 0.2"));
    m_toolbar->setWindowTitle(tx("操作", "Actions"));
    m_addAction->setText(tx("添加机器人", "Add robot"));
    m_removeAction->setText(tx("删除所选", "Remove selected"));
    m_refreshAction->setText(tx("刷新状态", "Refresh status"));
    m_downloadAction->setText(tx("下载当前地图", "Download current map"));
    m_openMapAction->setText(tx("打开地图", "Open map"));
    m_fitMapAction->setText(tx("适应窗口", "Fit map"));
    m_uploadAction->setText(tx("仅上传", "Upload only"));
    m_uploadSwitchAction->setText(tx("上传、验证并切换", "Upload, verify and switch"));
    m_languageLabel->setText(tx("语言：", "Language: "));
    m_table->setHorizontalHeaderLabels({tx("选择", "Select"), tx("名称", "Name"),
        QStringLiteral("IP"), tx("连接", "Connection"), tx("任务", "Task"),
        tx("当前地图", "Current map"), QStringLiteral("MD5"), tx("结果", "Result")});
    if (m_activeOperations == 0 && m_pendingRows.isEmpty())
        m_batchLabel->setText(tx("就绪。批量操作默认并发 3 台，繁忙机器人会跳过。",
                                 "Ready. Up to 3 robots run concurrently; busy robots are skipped."));
    m_log->setPlaceholderText(tx("操作日志", "Operation log"));
    m_tabs->setTabText(0, tx("地图预览", "Map preview"));
    m_tabs->setTabText(1, tx("操作日志", "Operation log"));
    updateMapSummary();
    statusBar()->showMessage(tx("真实切图前，请先在单台测试机器人验证端口和协议版本。",
                                "Verify ports and protocol version on one test robot before a real map switch."));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto *online = m_table->item(row, Online); online && online->data(Qt::UserRole).isValid()) {
            switch (online->data(Qt::UserRole).toInt()) {
            case 0: setCell(row, Online, tx("未检查", "Not checked")); break;
            case 1: setCell(row, Online, tx("在线", "Online")); break;
            case 2: setCell(row, Online, tx("离线", "Offline")); break;
            case 3: setCell(row, Online, tx("协议错误", "Protocol error")); break;
            }
        }
        if (auto *task = m_table->item(row, Task); task && task->data(Qt::UserRole).isValid())
            setCell(row, Task, statusName(task->data(Qt::UserRole).toInt(), m_english));
        if (auto *result = m_table->item(row, Result);
            result && result->data(Qt::UserRole).toInt() == 1) {
            setCell(row, Result, tx("等待操作", "Waiting"));
            m_table->item(row, Result)->setData(Qt::UserRole, 1);
        } else if (result) setCell(row, Result, knownResultText(result->text(), m_english));
    }
}

void MainWindow::loadRobots()
{
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("robots"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *check = new QCheckBox;
        auto *box = new QWidget;
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(check);
        m_table->setCellWidget(row, Selected, box);
        setCell(row, Name, settings.value(QStringLiteral("name")).toString());
        setCell(row, Ip, settings.value(QStringLiteral("host")).toString());
        setCell(row, Online, tx("未检查", "Not checked"));
        m_table->item(row, Online)->setData(Qt::UserRole, 0);
        setCell(row, Task, tx("未知", "Unknown"));
        m_table->item(row, Task)->setData(Qt::UserRole, -1);
        setCell(row, CurrentMap, QStringLiteral("-"));
        setCell(row, Md5, QStringLiteral("-"));
        setCell(row, Result, tx("等待操作", "Waiting"));
        m_table->item(row, Result)->setData(Qt::UserRole, 1);
    }
    settings.endArray();
}

void MainWindow::saveRobots() const
{
    QSettings settings;
    settings.remove(QStringLiteral("robots"));
    settings.beginWriteArray(QStringLiteral("robots"));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        settings.setArrayIndex(row);
        settings.setValue(QStringLiteral("name"), m_table->item(row, Name)->text());
        settings.setValue(QStringLiteral("host"), m_table->item(row, Ip)->text());
    }
    settings.endArray();
}

void MainWindow::addRobot()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tx("手动添加 AMR", "Add AMR manually"));
    QFormLayout form(&dialog);
    QLineEdit name;
    QLineEdit host;
    name.setPlaceholderText(tx("例如 AMR-01", "For example, AMR-01"));
    host.setPlaceholderText(tx("例如 172.17.5.2", "For example, 172.17.5.2"));
    form.addRow(tx("名称", "Name"), &name);
    form.addRow(tx("IPv4 地址", "IPv4 address"), &host);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QHostAddress address;
    if (name.text().trimmed().isEmpty() || !address.setAddress(host.text().trimmed())
        || address.protocol() != QAbstractSocket::IPv4Protocol) {
        QMessageBox::warning(this, tx("无法添加", "Cannot add robot"),
                             tx("请输入名称和有效的 IPv4 地址。",
                                "Enter a name and a valid IPv4 address."));
        return;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, Ip)->text() == host.text().trimmed()) {
            QMessageBox::warning(this, tx("重复地址", "Duplicate address"),
                                 tx("该 IP 已经存在。", "This IP address already exists."));
            return;
        }
    }

    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *check = new QCheckBox;
    check->setChecked(true);
    auto *box = new QWidget;
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setAlignment(Qt::AlignCenter);
    boxLayout->addWidget(check);
    m_table->setCellWidget(row, Selected, box);
    setCell(row, Name, name.text().trimmed());
    setCell(row, Ip, host.text().trimmed());
    setCell(row, Online, tx("未检查", "Not checked"));
    m_table->item(row, Online)->setData(Qt::UserRole, 0);
    setCell(row, Task, tx("未知", "Unknown"));
    m_table->item(row, Task)->setData(Qt::UserRole, -1);
    setCell(row, CurrentMap, QStringLiteral("-"));
    setCell(row, Md5, QStringLiteral("-"));
    setCell(row, Result, tx("等待操作", "Waiting"));
    m_table->item(row, Result)->setData(Qt::UserRole, 1);
    saveRobots();
}

void MainWindow::removeSelected()
{
    if (m_activeOperations > 0 || !m_pendingRows.isEmpty()) {
        QMessageBox::warning(this, tx("操作进行中", "Operation in progress"),
                             tx("批量操作完成前不能删除机器人。",
                                "Robots cannot be removed until the batch operation finishes."));
        return;
    }
    const auto rows = selectedRows();
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) m_table->removeRow(*it);
    saveRobots();
}

void MainWindow::refreshSelected()
{
    auto rows = selectedRows();
    if (rows.isEmpty()) {
        for (int row = 0; row < m_table->rowCount(); ++row) rows.append(row);
    }
    for (const int row : rows) {
        setCell(row, Result, tx("查询地图状态…", "Querying map status..."));
        const Robot robot = robotAt(row);
        query(row, robot.statusPort, RbkProtocol::QueryMap, {}, [this, row](RequestResult result) {
            if (!result.ok) {
                setCell(row, Online, tx("离线", "Offline"));
                m_table->item(row, Online)->setData(Qt::UserRole, 2);
                setCell(row, Result, localizedError(result.error));
                return;
            }
            QJsonObject json;
            QString error;
            if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
                setCell(row, Online, tx("协议错误", "Protocol error"));
                m_table->item(row, Online)->setData(Qt::UserRole, 3);
                setCell(row, Result, error);
                return;
            }
            setCell(row, Online, tx("在线", "Online"));
            m_table->item(row, Online)->setData(Qt::UserRole, 1);
            setCell(row, CurrentMap, json.value(QStringLiteral("current_map")).toString(QStringLiteral("-")));
            setCell(row, Md5, json.value(QStringLiteral("current_map_md5")).toString(QStringLiteral("-")));
            setCell(row, Result, tx("状态已刷新", "Status refreshed"));
        });
    }
}

void MainWindow::downloadMap()
{
    const auto rows = selectedRows();
    if (rows.size() != 1) {
        QMessageBox::information(this, tx("选择机器人", "Select a robot"),
                                 tx("下载地图时请只选择一台机器人。",
                                    "Select exactly one robot to download its current map."));
        return;
    }
    const int row = rows.first();
    QString mapName = m_table->item(row, CurrentMap)->text().trimmed();
    if (mapName.endsWith(QStringLiteral(".smap"), Qt::CaseInsensitive)) mapName.chop(5);
    static const QRegularExpression valid(QStringLiteral("^[0-9A-Za-z_-]+$"));
    if (!valid.match(mapName).hasMatch()) {
        QMessageBox::warning(this, tx("无法下载", "Cannot download"),
            tx("当前地图名称未知或不合法，请先刷新机器人状态。",
               "The current map name is unknown or invalid. Refresh the robot status first."));
        return;
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) directory = QDir::homePath();
    QString fileName = QDir(directory).filePath(mapName + QStringLiteral(".smap"));
    if (QFileInfo::exists(fileName)) {
        fileName = QDir(directory).filePath(QStringLiteral("%1_%2.smap").arg(
            mapName, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    }
    setCell(row, Result, tx("下载当前地图…", "Downloading current map..."));
    const Robot robot = robotAt(row);
    query(row, robot.configPort, RbkProtocol::DownloadMap,
          compactJson({{QStringLiteral("map_name"), mapName}}),
          [this, row, fileName](RequestResult result) {
        if (!result.ok) { setCell(row, Result, localizedError(result.error)); return; }
        QJsonObject possibleError;
        QString ignored;
        if (parseJson(result.payload, &possibleError, &ignored)
            && possibleError.contains(QStringLiteral("ret_code"))
            && possibleError.value(QStringLiteral("ret_code")).toInt() != 0) {
            setCell(row, Result, possibleError.value(QStringLiteral("err_msg")).toString(
                tx("机器人拒绝下载", "The robot rejected the download")));
            return;
        }
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly) || file.write(result.payload) != result.payload.size()) {
            setCell(row, Result, tx("无法保存文件：%1", "Cannot save file: %1")
                                     .arg(file.errorString()));
            return;
        }
        file.close();
        const QString md5 = QString::fromLatin1(QCryptographicHash::hash(
            result.payload, QCryptographicHash::Md5).toHex());
        setCell(row, Result, tx("下载完成，本地 MD5 %1", "Download complete. Local MD5: %1").arg(md5));
        log(tx("地图已保存：%1", "Map saved to: %1").arg(fileName));
        MapSummary summary;
        QString previewError;
        if (m_mapView->loadBytes(result.payload, &summary, &previewError)) {
            m_mapSummary = summary;
            m_hasMapSummary = true;
            updateMapSummary();
            m_tabs->setCurrentIndex(0);
        } else {
            log(tx("地图已下载，但预览解析失败：%1",
                   "The map was downloaded, but preview parsing failed: %1").arg(previewError));
        }
        QMessageBox::information(this, tx("下载完成", "Download complete"),
            tx("当前地图已保存到：\n%1\n\n如需改名，请在文件管理器中操作。",
               "The current map was saved to:\n%1\n\nRename it later in your file manager if needed.")
                .arg(fileName));
    }, 60000);
}

void MainWindow::openMapFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this,
        tx("打开地图", "Open map"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tx("SEER 地图 (*.smap);;JSON 文件 (*.json);;所有文件 (*)",
           "SEER maps (*.smap);;JSON files (*.json);;All files (*)"));
    if (fileName.isEmpty()) return;

    MapSummary summary;
    QString error;
    if (!m_mapView->loadFile(fileName, &summary, &error)) {
        QMessageBox::critical(this, tx("无法打开地图", "Cannot open map"),
                              tx("地图解析失败：%1", "Map parsing failed: %1").arg(error));
        return;
    }
    m_mapSummary = summary;
    m_hasMapSummary = true;
    updateMapSummary();
    m_tabs->setCurrentIndex(0);
    log(tx("已打开地图：%1", "Opened map: %1").arg(fileName));
}

void MainWindow::updateMapSummary()
{
    if (!m_mapInfoLabel) return;
    if (!m_hasMapSummary) {
        m_mapInfoLabel->setText(tx("尚未打开地图。可点击“打开地图”，或下载机器人当前地图后自动预览。",
                                   "No map is open. Click Open map, or download a robot's current map for automatic preview."));
        return;
    }
    m_mapInfoLabel->setText(
        tx("地图：%1    类型：%2    版本：%3    分辨率：%4 m    扫描点：%5    站点：%6    路径：%7    区域：%8",
           "Map: %1    Type: %2    Version: %3    Resolution: %4 m    Scan points: %5    Stations: %6    Paths: %7    Areas: %8")
            .arg(m_mapSummary.name.isEmpty() ? QStringLiteral("-") : m_mapSummary.name,
                 m_mapSummary.type.isEmpty() ? QStringLiteral("-") : m_mapSummary.type,
                 m_mapSummary.version.isEmpty() ? QStringLiteral("-") : m_mapSummary.version)
            .arg(m_mapSummary.resolution, 0, 'g', 6)
            .arg(m_mapSummary.normalPointCount)
            .arg(m_mapSummary.stationCount)
            .arg(m_mapSummary.pathCount)
            .arg(m_mapSummary.areaCount));
}

void MainWindow::chooseAndUpload(bool switchAfterUpload)
{
    if (selectedRows().isEmpty()) {
        QMessageBox::information(this, tx("未选择机器人", "No robot selected"),
                                 tx("请勾选至少一台目标机器人。",
                                    "Select at least one target robot."));
        return;
    }
    const QString fileName = QFileDialog::getOpenFileName(this,
        tx("选择 2D 地图", "Select a 2D map"), {},
        tx("SMAP (*.smap);;JSON (*.json)", "SMAP (*.smap);;JSON (*.json)"));
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tx("无法读取地图", "Cannot read map"), file.errorString());
        return;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (document.isNull() || !document.isObject()) {
        QMessageBox::critical(this, tx("地图格式错误", "Invalid map format"),
            tx("2D 地图不是有效的 JSON 对象：%1",
               "The 2D map is not a valid JSON object: %1").arg(parseError.errorString()));
        return;
    }
    const QJsonObject root = document.object();
    const QJsonObject header = root.value(QStringLiteral("header")).toObject();
    QString mapName = header.value(QStringLiteral("mapName")).toString().trimmed();
    if (mapName.isEmpty()) {
        mapName = header.value(QStringLiteral("map_name")).toString().trimmed();
    }
    if (mapName.isEmpty()) {
        mapName = QFileInfo(fileName).completeBaseName().trimmed();
    }
    while (mapName.endsWith(QStringLiteral(".smap"), Qt::CaseInsensitive)) {
        mapName.chop(5);
        mapName = mapName.trimmed();
    }
    if (mapName.isEmpty()) {
        QMessageBox::warning(this, tx("地图名称缺失", "Missing map name"),
            tx("无法从地图内容或文件名中确定地图名称。",
               "The map name could not be determined from the map content or filename."));
        return;
    }
    const QString md5 = QString::fromLatin1(QCryptographicHash::hash(bytes,
        QCryptographicHash::Md5).toHex());
    const QString action = switchAfterUpload ? tx("上传、校验并切换", "Upload, verify and switch")
                                             : tx("仅上传并校验", "Upload and verify only");
    if (QMessageBox::question(this, tx("确认批量操作", "Confirm batch operation"),
        tx("操作：%1\n地图：%2\nMD5：%3\n机器人：%4 台\n\n"
           "运行中、等待中或暂停的机器人将被跳过，不会自动取消任务。",
           "Operation: %1\nMap: %2\nMD5: %3\nRobots: %4\n\n"
           "Running, waiting or suspended robots will be skipped. Tasks will not be canceled automatically.")
            .arg(action, mapName, md5).arg(selectedRows().size())) != QMessageBox::Yes) return;
    startBatch(bytes, mapName, switchAfterUpload);
}

void MainWindow::startBatch(const QByteArray &mapBytes, const QString &mapName,
                            bool switchAfterUpload)
{
    if (m_activeOperations > 0 || !m_pendingRows.isEmpty()) {
        QMessageBox::warning(this, tx("操作进行中", "Operation in progress"),
                             tx("请等待当前批量操作完成。",
                                "Wait for the current batch operation to finish."));
        return;
    }
    m_mapBytes = mapBytes;
    m_mapName = mapName;
    m_mapMd5 = QString::fromLatin1(QCryptographicHash::hash(mapBytes,
                                                            QCryptographicHash::Md5).toHex());
    m_switchAfterUpload = switchAfterUpload;
    const auto rows = selectedRows();
    for (const int row : rows) {
        m_pendingRows.enqueue(row);
        setCell(row, Result, tx("等待预检", "Waiting for precheck"));
    }
    m_totalOperations = rows.size();
    m_completedOperations = 0;
    m_batchLabel->setText(tx("批量操作：0/%1", "Batch operation: 0/%1").arg(m_totalOperations));
    launchNext();
}

void MainWindow::launchNext()
{
    while (m_activeOperations < MaxConcurrentRobots && !m_pendingRows.isEmpty()) {
        const int row = m_pendingRows.dequeue();
        ++m_activeOperations;
        runRobotOperation(row);
    }
    if (m_activeOperations == 0 && m_pendingRows.isEmpty() && m_totalOperations > 0) {
        m_batchLabel->setText(tx("批量操作完成：%1/%1", "Batch operation complete: %1/%1")
                              .arg(m_totalOperations));
        m_mapBytes.clear();
        m_totalOperations = 0;
    }
}

void MainWindow::runRobotOperation(int row)
{
    setCell(row, Result, tx("检查导航状态…", "Checking navigation status..."));
    const Robot robot = robotAt(row);
    query(row, robot.statusPort, RbkProtocol::QueryTask,
          compactJson({{QStringLiteral("simple"), true}}),
          [this, row](RequestResult result) {
        if (!result.ok) { finishRobot(row, tx("预检失败：%1", "Precheck failed: %1").arg(localizedError(result.error)), false); return; }
        QJsonObject json;
        QString error;
        if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
            finishRobot(row, tx("预检失败：%1", "Precheck failed: %1").arg(error), false); return;
        }
        const int status = json.value(QStringLiteral("task_status")).toInt(-1);
        setCell(row, Task, statusName(status, m_english));
        m_table->item(row, Task)->setData(Qt::UserRole, status);
        if (status == 1 || status == 2 || status == 3 || status < 0) {
            finishRobot(row, tx("已跳过：导航状态 %1", "Skipped: navigation status is %1")
                                .arg(statusName(status, m_english)), false);
            return;
        }
        uploadRobot(row);
    });
}

void MainWindow::uploadRobot(int row)
{
    setCell(row, Result, tx("上传地图…", "Uploading map..."));
    const Robot robot = robotAt(row);
    query(row, robot.configPort, RbkProtocol::UploadMap, m_mapBytes,
          [this, row](RequestResult result) {
        if (!result.ok) { finishRobot(row, tx("上传失败：%1", "Upload failed: %1").arg(localizedError(result.error)), false); return; }
        QJsonObject json;
        QString error;
        if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
            finishRobot(row, tx("上传失败：%1", "Upload failed: %1").arg(error), false); return;
        }
        verifyUpload(row);
    }, 120000);
}

void MainWindow::verifyUpload(int row)
{
    setCell(row, Result, tx("校验机器人 MD5…", "Verifying robot MD5..."));
    const Robot robot = robotAt(row);
    query(row, robot.statusPort, RbkProtocol::QueryMapMd5,
          compactJson({{QStringLiteral("map_names"), QJsonArray{m_mapName + QStringLiteral(".smap")}}}),
          [this, row](RequestResult result) {
        if (!result.ok) { finishRobot(row, tx("MD5 查询失败：%1", "MD5 query failed: %1").arg(localizedError(result.error)), false); return; }
        QJsonObject json;
        QString error;
        if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
            finishRobot(row, tx("MD5 查询失败：%1", "MD5 query failed: %1").arg(error), false); return;
        }
        QString remoteMd5;
        const auto info = json.value(QStringLiteral("map_info")).toArray();
        for (const auto &value : info) {
            const auto item = value.toObject();
            if (item.value(QStringLiteral("name")).toString() == m_mapName + QStringLiteral(".smap")) {
                remoteMd5 = item.value(QStringLiteral("md5")).toString();
                break;
            }
        }
        if (remoteMd5.compare(m_mapMd5, Qt::CaseInsensitive) != 0) {
            finishRobot(row, tx("MD5 不一致：机器人 %1，本地 %2",
                                "MD5 mismatch: robot %1, local %2")
                        .arg(remoteMd5.isEmpty() ? tx("无返回", "no value") : remoteMd5, m_mapMd5), false);
            return;
        }
        if (m_switchAfterUpload) switchRobot(row);
        else finishRobot(row, tx("上传及 MD5 校验成功", "Upload and MD5 verification succeeded"), true);
    });
}

void MainWindow::switchRobot(int row)
{
    setCell(row, Result, tx("切换地图…", "Switching map..."));
    const Robot robot = robotAt(row);
    query(row, robot.controlPort, RbkProtocol::LoadMap,
          compactJson({{QStringLiteral("map_name"), m_mapName}}),
          [this, row](RequestResult result) {
        if (!result.ok) { finishRobot(row, tx("切换失败：%1", "Map switch failed: %1").arg(localizedError(result.error)), false); return; }
        QJsonObject json;
        QString error;
        if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
            finishRobot(row, tx("切换失败：%1", "Map switch failed: %1").arg(error), false); return;
        }
        verifyCurrentMap(row);
    }, 60000);
}

void MainWindow::verifyCurrentMap(int row)
{
    setCell(row, Result, tx("验证当前地图…", "Verifying current map..."));
    const Robot robot = robotAt(row);
    query(row, robot.statusPort, RbkProtocol::QueryMap, {},
          [this, row](RequestResult result) {
        if (!result.ok) { finishRobot(row, tx("切换后验证失败：%1", "Post-switch verification failed: %1").arg(localizedError(result.error)), false); return; }
        QJsonObject json;
        QString error;
        if (!parseJson(result.payload, &json, &error) || !responseSucceeded(json, &error)) {
            finishRobot(row, tx("切换后验证失败：%1", "Post-switch verification failed: %1").arg(error), false); return;
        }
        const QString current = json.value(QStringLiteral("current_map")).toString();
        const QString md5 = json.value(QStringLiteral("current_map_md5")).toString();
        setCell(row, CurrentMap, current);
        setCell(row, Md5, md5);
        const bool mapMatches = current == m_mapName || current == m_mapName + QStringLiteral(".smap");
        if (!mapMatches || md5.compare(m_mapMd5, Qt::CaseInsensitive) != 0) {
            finishRobot(row, tx("当前地图验证不一致", "Current map verification mismatch"), false);
            return;
        }
        finishRobot(row, tx("上传、校验和切换成功", "Upload, verification and switch succeeded"), true);
    });
}

void MainWindow::finishRobot(int row, const QString &result, bool success)
{
    setCell(row, Result, result);
    log(QStringLiteral("%1 (%2): %3").arg(robotAt(row).name, robotAt(row).host, result));
    auto *item = m_table->item(row, Result);
    item->setForeground(success ? QColor(0, 128, 50) : QColor(180, 50, 40));
    --m_activeOperations;
    ++m_completedOperations;
    m_batchLabel->setText(tx("批量操作：%1/%2", "Batch operation: %1/%2")
                          .arg(m_completedOperations).arg(m_totalOperations));
    launchNext();
}

void MainWindow::query(int row, quint16 port, quint16 command, const QByteArray &payload,
                       std::function<void(RequestResult)> callback, int timeoutMs)
{
    const Robot robot = robotAt(row);
    RequestJob::start(this, robot.host, port, command, payload, std::move(callback), timeoutMs);
}

Robot MainWindow::robotAt(int row) const
{
    Robot robot;
    robot.name = m_table->item(row, Name)->text();
    robot.host = m_table->item(row, Ip)->text();
    return robot;
}

QList<int> MainWindow::selectedRows() const
{
    QList<int> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *box = m_table->cellWidget(row, Selected);
        auto *check = box ? box->findChild<QCheckBox *>() : nullptr;
        if (check && check->isChecked()) result.append(row);
    }
    return result;
}

void MainWindow::setCell(int row, Column column, const QString &text)
{
    auto *item = m_table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        if (column != Selected) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, column, item);
    }
    item->setText(text);
    if (column == Result) item->setData(Qt::UserRole, QVariant());
    item->setForeground(palette().color(QPalette::Text));
}

void MainWindow::log(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}

bool MainWindow::parseJson(const QByteArray &bytes, QJsonObject *object, QString *error) const
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (!document.isObject()) {
        if (error) *error = tx("JSON 响应无效：%1", "Invalid JSON response: %1")
                                .arg(parseError.errorString());
        return false;
    }
    *object = document.object();
    return true;
}

bool MainWindow::responseSucceeded(const QJsonObject &object, QString *error) const
{
    const int code = object.value(QStringLiteral("ret_code")).toInt(0);
    if (code == 0) return true;
    if (error) *error = tx("错误 %1：%2", "Error %1: %2").arg(code)
        .arg(object.value(QStringLiteral("err_msg")).toString(tx("未知错误", "Unknown error")));
    return false;
}
