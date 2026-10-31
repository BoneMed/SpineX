/**
 * ============================================================================
 * LicenseManager - 在线许可证管理（CaaS 后端验证）
 * ============================================================================
 */

#ifndef SPINEX_LICENSE_MANAGER_H
#define SPINEX_LICENSE_MANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QJsonObject>

class LicenseManager : public QObject
{
    Q_OBJECT

public:
    static LicenseManager& instance();

    // 登录结果
    enum LoginResult { LoginOK, LoginFailed, LoginNoLicense, LoginPending };

    // 登录（同步，阻塞等待服务器响应）
    LoginResult login(const QString& email, const QString& password);

    // 验证 license（用已存的 token + license_key 调用服务器）
    bool verifyLicense();

    // 本地缓存是否有效（有 token + license 且未过期）
    bool hasValidCache() const;

    // 是否已激活
    bool isActivated() const { return activated_; }

    // License 信息
    QString tier() const { return tier_; }           // "shared" or "private"
    QString allowedModules() const { return allowedModules_; }  // CSV; empty → all allowed
    bool isModuleAllowed(const QString& moduleKey) const;
    QDateTime expiresAt() const { return expiresAt_; }
    int daysRemaining() const;
    QString userEmail() const;
    QString userName() const;
    QString storedToken() const { return token_; }

    // 登出（清除本地缓存）
    void logout();

    // 启动运行期定时检查
    void startPeriodicCheck();

Q_SIGNALS:
    void licenseExpired();

private:
    LicenseManager();
    QString machineFingerprint() const;

    // 同步 HTTP 请求
    QJsonObject httpPost(const QString& endpoint, const QJsonObject& body,
                         const QString& bearerToken = {});
    QJsonObject httpGet(const QString& endpoint, const QString& bearerToken);

    // 本地持久化
    void saveCache();
    void loadCache();

    void checkExpiry();

    bool activated_;
    QString token_;          // JWT token
    QString licenseKey_;     // UUID license key
    QString tier_;           // "shared" / "private"
    QString allowedModules_; // CSV of module keys, empty → all
    QDateTime expiresAt_;    // license 到期时间
    QString email_;
    QString name_;

    QTimer* checkTimer_;

    static const QString API_BASE;
};

#endif // SPINEX_LICENSE_MANAGER_H
