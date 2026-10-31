/**
 * UpdateChecker implementation
 */
#include "UpdateChecker.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QProcess>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>

static const QString API_BASE = "https://www.spinex.cn";

// ============================================================
// Static helpers
// ============================================================
QString UpdateChecker::currentVersion()
{
    // CMake sets PROJECT_VERSION; embed as compile-time constant or read from config
    // For now hard-code to match installer version
    return QStringLiteral("1.0.5");
}

int UpdateChecker::compareVersions(const QString& a, const QString& b)
{
    QStringList pa = a.split('.');
    QStringList pb = b.split('.');
    int n = std::max(pa.size(), pb.size());
    for (int i = 0; i < n; i++) {
        int va = i < pa.size() ? pa[i].toInt() : 0;
        int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) return va - vb;
    }
    return 0;
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

// ============================================================
// Main flow
// ============================================================
void UpdateChecker::checkForUpdate(QWidget* parent, bool silent)
{
    VersionInfo info = fetchLatest();
    if (info.latest.isEmpty()) {
        if (!silent)
            QMessageBox::warning(parent, "检查更新", "无法连接到更新服务器。");
        return;
    }

    QString local = currentVersion();
    int cmp = compareVersions(info.latest, local);
    if (cmp <= 0) {
        if (!silent)
            QMessageBox::information(parent, "检查更新",
                QString("当前已是最新版本 (%1)").arg(local));
        return;
    }

    // New version available — show dialog
    QDialog dlg(parent);
    dlg.setWindowTitle("发现新版本");
    dlg.resize(500, 400);
    auto* layout = new QVBoxLayout(&dlg);

    auto* title = new QLabel(QString("<h2>SpineX %1 可用</h2>").arg(info.latest));
    layout->addWidget(title);

    auto* sub = new QLabel(QString("当前版本: %1  →  新版本: %2  (%3 MB)")
        .arg(local).arg(info.latest).arg(info.size / 1024 / 1024));
    sub->setStyleSheet("color:#6b7280;");
    layout->addWidget(sub);

    layout->addSpacing(8);
    layout->addWidget(new QLabel("<b>更新内容：</b>"));

    auto* notes = new QTextEdit();
    notes->setPlainText(info.notes.isEmpty() ? "(无说明)" : info.notes);
    notes->setReadOnly(true);
    notes->setStyleSheet("background:#f9fafb;border:1px solid #e5e7eb;border-radius:4px;");
    layout->addWidget(notes, 1);

    if (info.mandatory) {
        auto* warn = new QLabel("⚠️ <b>此更新为强制更新</b>，必须安装才能继续使用。");
        warn->setStyleSheet("color:#dc2626;padding:8px;background:#fee2e2;border-radius:4px;");
        layout->addWidget(warn);
    }

    auto* btns = new QHBoxLayout();
    btns->addStretch();
    auto* laterBtn = new QPushButton(info.mandatory ? "退出" : "稍后");
    laterBtn->setStyleSheet("padding:8px 20px;");
    btns->addWidget(laterBtn);
    auto* updateBtn = new QPushButton("立即更新");
    updateBtn->setStyleSheet(
        "QPushButton { padding:8px 20px; background:#10b981; color:white; "
        "font-weight:bold; border-radius:4px; border:none; }"
        "QPushButton:hover { background:#059669; }");
    btns->addWidget(updateBtn);
    layout->addLayout(btns);

    connect(laterBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(updateBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    int result = dlg.exec();
    if (result != QDialog::Accepted) {
        if (info.mandatory) {
            QCoreApplication::quit();
        }
        return;
    }

    // Download + install
    QString installerPath;
    if (!downloadInstaller(parent, info, installerPath)) return;

    if (!verifyChecksum(installerPath, info.sha256)) {
        QMessageBox::critical(parent, "校验失败",
            "下载的安装文件校验失败，可能已损坏。请稍后重试。");
        QFile::remove(installerPath);
        return;
    }

    QMessageBox::information(parent, "准备安装",
        "SpineX 即将关闭并启动安装程序。\n安装完成后会自动重新打开。");

    launchInstaller(installerPath);
    QCoreApplication::quit();
}

// ============================================================
// Fetch latest version info (sync)
// ============================================================
UpdateChecker::VersionInfo UpdateChecker::fetchLatest()
{
    VersionInfo info;
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(API_BASE + "/api/version"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = mgr.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return info;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject o = doc.object();
    info.latest = o.value("latest").toString();
    info.url = o.value("url").toString();
    info.sha256 = o.value("sha256").toString();
    info.size = (qint64)o.value("size").toDouble();
    info.notes = o.value("notes").toString();
    info.mandatory = o.value("mandatory").toBool();
    reply->deleteLater();
    return info;
}

// ============================================================
// Download with progress dialog
// ============================================================
bool UpdateChecker::downloadInstaller(QWidget* parent, const VersionInfo& info, QString& outPath)
{
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString filename = QString("SpineX_Setup_v%1_%2.exe")
        .arg(info.latest)
        .arg(QDateTime::currentMSecsSinceEpoch());
    outPath = tmpDir + "/" + filename;

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, "下载失败", "无法创建临时文件");
        return false;
    }

    QProgressDialog progress("正在下载更新...", "取消", 0, 100, parent);
    progress.setWindowTitle("SpineX 更新");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    progress.show();

    QNetworkAccessManager mgr;
    QNetworkRequest req((QUrl(info.url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = mgr.get(req);

    bool cancelled = false;
    connect(&progress, &QProgressDialog::canceled, [&]() {
        cancelled = true;
        reply->abort();
    });

    connect(reply, &QNetworkReply::downloadProgress,
        [&](qint64 got, qint64 total) {
            if (total > 0) {
                int pct = (int)(got * 100 / total);
                progress.setValue(pct);
                progress.setLabelText(QString("正在下载更新... %1 / %2 MB")
                    .arg(got / 1024 / 1024).arg(total / 1024 / 1024));
            }
        });

    connect(reply, &QNetworkReply::readyRead,
        [&]() { file.write(reply->readAll()); });

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    file.write(reply->readAll());
    file.close();

    bool ok = (reply->error() == QNetworkReply::NoError) && !cancelled;
    if (!ok) {
        QFile::remove(outPath);
        if (!cancelled)
            QMessageBox::critical(parent, "下载失败",
                QString("下载错误: %1").arg(reply->errorString()));
    }
    reply->deleteLater();
    progress.close();
    return ok;
}

// ============================================================
// SHA256 verify
// ============================================================
bool UpdateChecker::verifyChecksum(const QString& filePath, const QString& expectedSha256)
{
    if (expectedSha256.isEmpty()) return true;  // skip if server didn't provide

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    const qint64 chunk = 1024 * 1024;
    while (!f.atEnd()) {
        hasher.addData(f.read(chunk));
    }
    f.close();
    QString got = hasher.result().toHex();
    return got.compare(expectedSha256, Qt::CaseInsensitive) == 0;
}

// ============================================================
// Launch installer
// ============================================================
void UpdateChecker::launchInstaller(const QString& installerPath)
{
    // /SILENT = minimal UI; /CLOSEAPPLICATIONS = auto-close running SpineX;
    // /RESTARTAPPLICATIONS = restart after install
    QStringList args = {"/SILENT", "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS"};
    QProcess::startDetached(installerPath, args);
}
