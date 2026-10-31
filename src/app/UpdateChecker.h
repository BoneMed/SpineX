/**
 * ============================================================================
 * UpdateChecker - Auto-update client
 *
 * Checks /api/version on startup, prompts user if newer version available,
 * downloads installer with progress bar, launches with /SILENT /CLOSEAPPLICATIONS.
 * ============================================================================
 */

#ifndef SPINEX_UPDATE_CHECKER_H
#define SPINEX_UPDATE_CHECKER_H

#include <QObject>
#include <QString>
#include <QWidget>

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    struct VersionInfo {
        QString latest;        // "1.1.0"
        QString url;
        QString sha256;
        qint64 size = 0;
        QString notes;
        bool mandatory = false;
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    // Check server for new version. If found, show dialog + download + install.
    // `silent` = don't show "already up to date" message.
    void checkForUpdate(QWidget* parent, bool silent = true);

    // Current local version (from CMake AppVersion, e.g. "1.0.1")
    static QString currentVersion();

    // Compare semver: returns >0 if a > b, 0 if equal, <0 if a < b
    static int compareVersions(const QString& a, const QString& b);

private:
    VersionInfo fetchLatest();
    bool downloadInstaller(QWidget* parent, const VersionInfo& info, QString& outPath);
    bool verifyChecksum(const QString& filePath, const QString& expectedSha256);
    void launchInstaller(const QString& installerPath);
};

#endif // SPINEX_UPDATE_CHECKER_H
