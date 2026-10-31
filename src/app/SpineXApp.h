/**
 * ============================================================================
 * SpineXApp - 主窗口（对应 APP_Main.py 的 SpinalyzsPyQt）
 * ============================================================================
 */

#ifndef SPINEX_APP_H
#define SPINEX_APP_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QSplitter>
#include <QVariant>

#include "AppConfig.h"
#include "ui/SharedViewport.h"
#include "ui/WelcomeScreen.h"
#include "modules/ModuleBase.h"

#include <map>

// 前向声明
class MenuBar;

class SpineXApp : public QMainWindow
{
    Q_OBJECT

public:
    explicit SpineXApp(QWidget* parent = nullptr);
    ~SpineXApp();

    // 页面切换
    void showWelcomeScreen();
    void showWorkspace();

    // 模块管理 — 懒加载 (get or create)
    void switchToModule(const QString& moduleKey);

    // 视口控制
    void setViewportVisible(bool visible);
    SharedViewport* sharedViewport() { return sharedViewport_; }

    // 模块实例访问
    ModuleBase* moduleInstance(const QString& key) const;

    // 应用配置
    AppConfig& config() { return config_; }

    // 当前项目数据（跨模块共享）
    QVariantMap currentProject;
    int dataVersion = 0;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void initUI();

    AppConfig config_;

    // UI
    QStackedWidget* stackedWidget_;
    WelcomeScreen* welcomeScreen_;
    QWidget* workspace_;
    QSplitter* splitter_;
    QStackedWidget* moduleControlStack_;
    SharedViewport* sharedViewport_;
    MenuBar* menuBar_;

    // 模块
    std::map<QString, ModuleBase*> moduleInstances_;
    ModuleBase* currentModule_;
};

#endif // SPINEX_APP_H
