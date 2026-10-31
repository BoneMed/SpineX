/**
 * ============================================================================
 * SpineX 主入口文件 (main.cpp)
 * ============================================================================
 */

// pybind11 must be included BEFORE Qt headers (Python "slots" macro conflict)
#include <pybind11/embed.h>
namespace py = pybind11;

#ifdef slots
#undef slots
#endif


#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QPalette>
#include <QColor>
#include <QDialog>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>
#include <QTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QProcess>
#include <QCoreApplication>
#include <QSettings>
#include <QEvent>
#include <iostream>

// ── 对话框按钮样式修复器 ──────────────────────────────────────────────────────
// Qt6 Fusion 样式下，QSS 对 QMessageBox/QInputDialog 等内置对话框的按钮
// 不稳定。用 event filter 在每个对话框 Show 时直接给所有子按钮设样式。
class DialogButtonStyler : public QObject
{
public:
    explicit DialogButtonStyler(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::Show) {
            if (qobject_cast<QDialog*>(obj)) {
                static const QString btnStyle = QStringLiteral(
                    "QPushButton {"
                    "  background-color: #2c5f6f;"
                    "  color: white;"
                    "  border: none;"
                    "  min-width: 80px;"
                    "  min-height: 32px;"
                    "  padding: 0 16px;"
                    "  border-radius: 6px;"
                    "  font-size: 10pt;"
                    "  font-weight: bold;"
                    "}"
                    "QPushButton:hover  { background-color: #1e4a57; }"
                    "QPushButton:pressed{ background-color: #163d4a; }"
                    "QPushButton:disabled{ background-color: #d1d5db; color: #9ca3af; }"
                );
                for (auto* btn : obj->findChildren<QPushButton*>())
                    btn->setStyleSheet(btnStyle);
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

#include <QVTKOpenGLNativeWidget.h>

#include "bridge/PythonRuntime.h"
#include "app/SpineXApp.h"
#include "app/LicenseManager.h"
#include "ui/LicenseDialog.h"

int main(int argc, char* argv[])
{
    // Detect multiprocessing child processes and exit immediately
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.find("--multiprocessing") != std::string::npos ||
            arg.find("from multiprocessing") != std::string::npos ||
            arg.find("semaphore_tracker") != std::string::npos) {
            return 0;
        }
    }

    QSurfaceFormat format = QVTKOpenGLNativeWidget::defaultFormat();
    format.setSamples(4);   // 4x MSAA 抗锯齿
    QSurfaceFormat::setDefaultFormat(format);

    // 确保 OpenSSL DLL 在 PATH 中（HTTPS 支持）— Windows 专属

    // 第2步：创建 Qt 应用程序
    QApplication app(argc, argv);
    app.setStyle("Fusion");   // ensures QSS fully applies on all platforms

    // Create named mutex so Inno Setup installer can detect/close running instance

    // Set palette so ALL buttons (including built-in QMessageBox/QDialog/
    // QProgressDialog buttons) always have a visible dark background + white text.
    // QSS can override this for specific widgets, but palette acts as the fallback.
    QPalette pal = app.palette();
    pal.setColor(QPalette::Button,       QColor(0x2c, 0x5f, 0x6f)); // teal bg
    pal.setColor(QPalette::ButtonText,   Qt::white);
    pal.setColor(QPalette::Window,       QColor(0xf4, 0xf6, 0xf8));
    pal.setColor(QPalette::WindowText,   QColor(0x1a, 0x23, 0x32));
    pal.setColor(QPalette::Base,         Qt::white);
    pal.setColor(QPalette::AlternateBase,QColor(0xf9, 0xfa, 0xfb));
    pal.setColor(QPalette::Text,         QColor(0x1a, 0x23, 0x32));
    pal.setColor(QPalette::Highlight,    QColor(0x2c, 0x5f, 0x6f));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);

    // 安装对话框按钮样式修复器（对所有 QDialog 子类生效）
    app.installEventFilter(new DialogButtonStyler(&app));

    app.setApplicationName("SpineX");
    app.setOrganizationName(QString::fromUtf8("\u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8"));
    app.setWindowIcon(QIcon("resources/Fig/logo.ico"));

    // 免责声明（首次启动时弹出，接受后不再显示）
    {
        QSettings disclaimerSettings("BoneMed", "SpineX");
        if (!disclaimerSettings.value("disclaimer_accepted", false).toBool()) {
            QMessageBox disclaimer;
            disclaimer.setWindowTitle("SpineX - Disclaimer");
            disclaimer.setIcon(QMessageBox::Warning);
            disclaimer.setTextFormat(Qt::RichText);
            disclaimer.setText(
                "<h3 style='color:#2c5f6f;'>Research Use Only</h3>"
                "<p>This software is intended <b>solely for research and educational purposes</b>.</p>"
                "<p style='color:#ef4444;'><b>NOT for clinical use, medical diagnosis, "
                "or treatment decisions.</b></p>"
                "<p>SpineX has not been approved or cleared by any regulatory authority "
                "(e.g., FDA, CE, NMPA) for clinical or diagnostic use. "
                "The results generated by this software should not be used as a basis "
                "for medical decisions.</p>"
                "<p>By clicking 'I Accept', you acknowledge and agree to these terms.</p>"
                "<hr>"
                "<p style='font-size:10px; color:#999;'>"
                "&copy; 2025-2026 \u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8 All Rights Reserved.</p>"
            );
            disclaimer.addButton("I Accept", QMessageBox::AcceptRole);
            auto* declineBtn = disclaimer.addButton("Decline", QMessageBox::RejectRole);

            disclaimer.exec();

            if (disclaimer.clickedButton() == declineBtn) {
                return 0;
            }

            disclaimerSettings.setValue("disclaimer_accepted", true);
            disclaimerSettings.sync();
        }
    }

    // 第3步：检测并初始化 Python 运行环境
    {
        // ---- Linux：在常见 conda/miniforge 安装路径下找 BoneMed env ----
        QString boneMedPath;
        QStringList candidates = {
            qEnvironmentVariable("SPINEX_BONEMED"),
            QDir::homePath() + "/miniforge3/envs/BoneMed",
            QDir::homePath() + "/miniconda3/envs/BoneMed",
            QDir::homePath() + "/anaconda3/envs/BoneMed",
            "/opt/miniforge3/envs/BoneMed",
            "/opt/miniconda3/envs/BoneMed",
            "/opt/anaconda3/envs/BoneMed",
        };
        for (const auto& p : candidates) {
            if (!p.isEmpty() && QFileInfo::exists(p + "/bin/python3.10")) {
                boneMedPath = p;
                break;
            }
        }
        if (boneMedPath.isEmpty()) {
            QMessageBox::critical(nullptr, "SpineX - Environment Error",
                "BoneMed conda environment not found.\n\n"
                "Expected at one of:\n"
                "  ~/miniforge3/envs/BoneMed\n"
                "  ~/miniconda3/envs/BoneMed\n"
                "  ~/anaconda3/envs/BoneMed\n\n"
                "Or set the SPINEX_BONEMED environment variable to override.\n\n"
                "Create with:\n"
                "  conda create -n BoneMed python=3.10");
            return 1;
        }

        // 检测 GPU（通过检查 torch CUDA）
        try {
            PythonRuntime::instance().initialize(boneMedPath.toStdString());
        }
        catch (const std::exception& e) {
            std::cerr << "[SpineX] Python init failed: " << e.what() << std::endl;
            QMessageBox::critical(nullptr, "SpineX - Python Error",
                QString("Failed to initialize Python runtime.\n\n%1").arg(e.what()));
            return 1;
        }
    }

    // 第4步：License 在线验证（每次启动都必须在线验证）
    auto& lic = LicenseManager::instance();
    bool verified = false;

    if (lic.hasValidCache()) {
        verified = lic.verifyLicense();
        if (!verified) {
            lic.logout();  // 清除过期缓存
        }
    }

    if (!verified) {
        LicenseDialog licenseDlg;
        if (licenseDlg.exec() != QDialog::Accepted || !licenseDlg.wasActivated()) {
            return 0;
        }
    }

    // 第5步：创建主窗口并显示
    SpineXApp mainWindow;
    mainWindow.show();
    mainWindow.showMaximized();

    // 第6步：运行事件循环
    int result = app.exec();

    // 第7步：清理 Python
    PythonRuntime::instance().shutdown();

    return result;
}
