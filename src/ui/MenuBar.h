/**
 * ============================================================================
 * MenuBar - 菜单栏导航（对应 ui/menu_bar.py）
 * ============================================================================
 * Python 版本中 MenuBar 不是 QMenuBar 子类，而是一个普通类，
 * 操作 main_window.menuBar()。C++ 版本保持相同模式。
 * ============================================================================
 */

#ifndef SPINEX_MENU_BAR_H
#define SPINEX_MENU_BAR_H

#include <QObject>
#include <QMenuBar>
#include <QMenu>
#include <QAction>

// 前向声明
class SpineXApp;

class MenuBar : public QObject
{
    Q_OBJECT

public:
    MenuBar(SpineXApp* mainWindow);

    void updateMenuTitle(const QString& title);

private:
    void applyMenuBarStyle();
    void createFileMenu();
    void createSaveMenu();
    void createModulesMenu();
    void createAddonsMenu();
    void createHelpMenu();

    // Module management
    void goToHome();
    void openDataModule();
    void openCTSegmentation();
    void openModulusEstimation();
    void openStressPrediction();

    // File operations
    void newProject();
    void loadProject();
    void dataLoading();
    void saveProject();
    void saveMask();
    void exportBMDResults();
    void exportMeshInp();
    void exportStressResults();

    // Help
    void showHelp();
    void showAbout();
    void showThirdPartyLicenses();
    void signOut();

    // Add-ons
    void openCobbAngleAddon();

    SpineXApp* mainWindow_;
    QMenuBar* menubar_;
    QMenu* modulesMenu_;

    // Module menu actions (for permission-based visibility)
    QAction* dataAction_ = nullptr;
    QAction* ctAction_ = nullptr;
    QAction* modulusAction_ = nullptr;
    QAction* stressAction_ = nullptr;
    QAction* cobbAction_ = nullptr;

    void applyModulePermissions();
};

#endif // SPINEX_MENU_BAR_H
