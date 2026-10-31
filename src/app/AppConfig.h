/**
 * ============================================================================
 * AppConfig - 应用配置和样式（对应 AppUtils/config.py）
 * ============================================================================
 */

#ifndef SPINEX_APP_CONFIG_H
#define SPINEX_APP_CONFIG_H

#include <QString>
#include <QMap>

class AppConfig
{
public:
    AppConfig();

    // 获取全局 QSS 样式表
    QString stylesheet() const { return stylesheet_; }

    // 颜色访问
    QString color(const QString& key) const;

    // 支持的文件格式
    QString fileFilter(const QString& key) const;

private:
    void initColors();
    void initFileTypes();
    QString createStylesheet() const;

    QMap<QString, QString> colors_;
    QMap<QString, QString> fileTypes_;
    QString stylesheet_;
};

#endif // SPINEX_APP_CONFIG_H
