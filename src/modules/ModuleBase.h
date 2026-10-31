/**
 * ============================================================================
 * ModuleBase - 模块抽象基类
 * ============================================================================
 */

#ifndef SPINEX_MODULE_BASE_H
#define SPINEX_MODULE_BASE_H

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QString>

class SharedViewport;
class SpineXApp;

class ModuleBase : public QWidget
{
    Q_OBJECT

public:
    explicit ModuleBase(const QString& name, QWidget* parent = nullptr);
    virtual ~ModuleBase();

    // 从项目数据刷新
    virtual void refreshFromProject();

    // 通用接口
    QString getName() const;
    void setApp(SpineXApp* app);

    // 模块头部（Python _make_module_header 的等价物）
    static QWidget* makeModuleHeader(const QString& titleText);

protected:
    QString name_;
    SpineXApp* app_;

Q_SIGNALS:
    void statusMessage(const QString& message);
};

#endif // SPINEX_MODULE_BASE_H
