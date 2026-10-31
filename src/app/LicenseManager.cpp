/**
 * ============================================================================
 * LicenseManager - 在线许可证管理（实现）
 * ============================================================================
 * 通过 CaaS 后端 API 验证用户身份和许可证。
 * 本地缓存 token + license 信息，定时在线验证。
 * ============================================================================
 */

#include "LicenseManager.h"

#include <QSettings>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>
#include <iostream>

// ---- 后端 API 地址 ----
const QString LicenseManager::API_BASE = "https://www.spinex.cn";

LicenseManager& LicenseManager::instance()
{
    static LicenseManager mgr;
    return mgr;
}

LicenseManager::LicenseManager()
    : QObject(nullptr)
    , activated_(false)
    , checkTimer_(nullptr)
{
    loadCache();

    // 如果本地有缓存且未过期，标记为已激活
    if (hasValidCache()) {
        activated_ = true;
    }
}

// ---- 同步 HTTP 请求 ----

QJsonObject LicenseManager::httpPost(const QString& endpoint,
                                      const QJsonObject& body,
                                      const QString& bearerToken)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(API_BASE + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!bearerToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + bearerToken).toUtf8());

    QByteArray data = QJsonDocument(body).toJson();
    QNetworkReply* reply = mgr.post(req, data);

    // 同步等待（用 QEventLoop）
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // 超时 15 秒
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject result;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        result = QJsonDocument::fromJson(reply->readAll()).object();
        result["_status"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    } else {
        result["_error"] = reply->errorString();
        result["_status"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 尝试解析 body
        QByteArray respData = reply->readAll();
        if (!respData.isEmpty()) {
            QJsonObject errBody = QJsonDocument::fromJson(respData).object();
            if (errBody.contains("detail"))
                result["detail"] = errBody["detail"];
        }
    }
    reply->deleteLater();
    return result;
}

QJsonObject LicenseManager::httpGet(const QString& endpoint,
                                     const QString& bearerToken)
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(API_BASE + endpoint));
    if (!bearerToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + bearerToken).toUtf8());

    QNetworkReply* reply = mgr.get(req);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject result;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        // 可能返回数组，包在 object 里
        QByteArray raw = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject()) result = doc.object();
        else result["_array"] = doc.array();
        result["_status"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    } else {
        result["_error"] = reply->errorString();
        result["_status"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    reply->deleteLater();
    return result;
}

// ---- 登录 ----

LicenseManager::LoginResult LicenseManager::login(const QString& email, const QString& password)
{
    QJsonObject body;
    body["email"] = email;
    body["password"] = password;

    QJsonObject resp = httpPost("/api/auth/login", body);

    if (!resp.contains("access_token")) {
        QString detail = resp.value("detail").toString();
        // 区分"待审核"和"密码错误"
        if (detail.contains("审核")) return LoginPending;
        return LoginFailed;
    }

    token_ = resp["access_token"].toString();
    email_ = email;

    // 获取用户信息
    QJsonObject me = httpGet("/api/auth/me", token_);
    if (me.contains("name"))
        name_ = me["name"].toString();

    // 获取 license 列表，找第一个有效的
    QJsonObject licResp = httpGet("/api/license/info", token_);
    QJsonArray licenses = licResp.value("_array").toArray();

    licenseKey_.clear();
    for (const auto& val : licenses) {
        QJsonObject lic = val.toObject();
        if (lic["is_active"].toBool()) {
            licenseKey_ = lic["license_key"].toString();
            tier_ = lic["tier"].toString();
            expiresAt_ = QDateTime::fromString(lic["end_date"].toString(), Qt::ISODate);
            break;
        }
    }

    if (licenseKey_.isEmpty()) {
        saveCache();  // 保存 token，让 Request License 能用
        return LoginNoLicense;
    }

    // 在线验证 license（同时注册设备）
    if (!verifyLicense()) {
        return LoginNoLicense;
    }

    activated_ = true;
    saveCache();
    return LoginOK;
}

// ---- 在线验证 license ----

bool LicenseManager::isModuleAllowed(const QString& moduleKey) const
{
    // Empty list = full access (backward compat)
    if (allowedModules_.trimmed().isEmpty()) return true;
    QStringList mods = allowedModules_.split(',', Qt::SkipEmptyParts);
    for (QString& m : mods) m = m.trimmed();
    return mods.contains(moduleKey);
}

bool LicenseManager::verifyLicense()
{
    if (licenseKey_.isEmpty()) return false;

    QJsonObject body;
    body["license_key"] = licenseKey_;
    body["machine_fingerprint"] = machineFingerprint();

    QJsonObject resp = httpPost("/api/license/verify", body);

    bool valid = resp.value("valid").toBool(false);
    if (valid) {
        tier_ = resp.value("tier").toString();
        expiresAt_ = QDateTime::fromString(resp.value("expires_at").toString(), Qt::ISODate);
        allowedModules_ = resp.value("allowed_modules").toString();
    } else {
    }

    return valid;
}

// ---- 本地缓存 ----

bool LicenseManager::hasValidCache() const
{
    if (token_.isEmpty() || licenseKey_.isEmpty()) return false;
    if (!expiresAt_.isValid()) return false;
    return QDateTime::currentDateTime() < expiresAt_;
}

void LicenseManager::saveCache()
{
    QSettings s("BoneMed", "SpineX");
    s.setValue("auth/token", token_);
    s.setValue("auth/email", email_);
    s.setValue("auth/name", name_);
    s.setValue("license/key", licenseKey_);
    s.setValue("license/tier", tier_);
    s.setValue("license/expires", expiresAt_.toString(Qt::ISODate));
    s.setValue("license/modules", allowedModules_);
    s.sync();
}

void LicenseManager::loadCache()
{
    QSettings s("BoneMed", "SpineX");
    token_ = s.value("auth/token").toString();
    email_ = s.value("auth/email").toString();
    name_ = s.value("auth/name").toString();
    licenseKey_ = s.value("license/key").toString();
    tier_ = s.value("license/tier").toString();
    expiresAt_ = QDateTime::fromString(s.value("license/expires").toString(), Qt::ISODate);
    allowedModules_ = s.value("license/modules").toString();
}

void LicenseManager::logout()
{
    QSettings s("BoneMed", "SpineX");
    s.clear();
    s.sync();

    token_.clear();
    email_.clear();
    name_.clear();
    licenseKey_.clear();
    tier_.clear();
    expiresAt_ = QDateTime();
    activated_ = false;
}

QString LicenseManager::userEmail() const { return email_; }
QString LicenseManager::userName() const { return name_; }

int LicenseManager::daysRemaining() const
{
    if (!expiresAt_.isValid()) return -1;
    int days = QDateTime::currentDateTime().daysTo(expiresAt_);
    return days >= 0 ? days : -1;
}

// ---- 机器指纹 ----

QString LicenseManager::machineFingerprint() const
{
    QString computerName = qEnvironmentVariable("COMPUTERNAME",
                           qEnvironmentVariable("HOSTNAME", "unknown"));
    QString userName = qEnvironmentVariable("USERNAME",
                       qEnvironmentVariable("USER", "unknown"));
    QString homePath = QDir::homePath();

    QString raw = computerName + "|" + userName + "|" + homePath;
    return QString(QCryptographicHash::hash(raw.toUtf8(),
        QCryptographicHash::Sha256).toHex());
}

// ---- 运行期定时检查 ----

void LicenseManager::startPeriodicCheck()
{
    if (checkTimer_) return;

    checkTimer_ = new QTimer(this);
    connect(checkTimer_, &QTimer::timeout, this, &LicenseManager::checkExpiry);
    checkTimer_->start(300 * 1000);  // 每 5 分钟在线验证一次
}

void LicenseManager::checkExpiry()
{
    // 先检查本地缓存是否过期
    if (!hasValidCache()) {
        activated_ = false;
        if (checkTimer_) checkTimer_->stop();
        Q_EMIT licenseExpired();
        return;
    }

    // 在线验证
    if (!verifyLicense()) {
        activated_ = false;
        if (checkTimer_) checkTimer_->stop();
        Q_EMIT licenseExpired();
    }
}
