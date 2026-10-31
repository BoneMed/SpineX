/**
 * ============================================================================
 * LicenseDialog - 登录/注册对话框
 * ============================================================================
 */

#ifndef SPINEX_LICENSE_DIALOG_H
#define SPINEX_LICENSE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QStackedWidget>

class LicenseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(QWidget* parent = nullptr);

    bool wasActivated() const { return activated_; }

private:
    void onLogin();
    void onRegister();
    void onRequestLicense();
    void checkActivation();
    void showLoginPage();
    void showRegisterPage();
    void showPendingPage();
    void showNoLicensePage();

    QStackedWidget* pages_;

    // Login page
    QLineEdit* loginEmail_;
    QLineEdit* loginPassword_;
    QLabel* loginStatus_;

    // Register page
    QLineEdit* regName_;
    QLineEdit* regEmail_;
    QLineEdit* regPassword_;
    QLabel* regStatus_;
    QLabel* noLicStatus_;

    QString selectedTier_;      // "community" or "professional"
    QString registeredEmail_;   // 注册后暂存邮箱
    QString registeredPassword_;  // 注册后暂存密码
    bool activated_;

    // Pending 页面
    QLabel* pendingStatusLabel_;
    QPushButton* pendingSignInBtn_;
    QTimer* pendingCheckTimer_;
};

#endif // SPINEX_LICENSE_DIALOG_H
