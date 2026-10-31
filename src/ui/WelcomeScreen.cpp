/**
 * ============================================================================
 * WelcomeScreen - 欢迎页面（实现）
 * 从 Python ui/welcome_screen.py 1:1 移植
 * ============================================================================
 */

#include "WelcomeScreen.h"

#include <QVBoxLayout>
#include <QFrame>
#include <QPainter>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <algorithm>
#include <iostream>

WelcomeScreen::WelcomeScreen(QWidget* parent)
    : QWidget(parent)
{
    loadLogo();
    initUI();
}

void WelcomeScreen::loadLogo()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList searchPaths = {
        appDir + "/resources/Fig/logo.ico",
        appDir + "/../resources/Fig/logo.ico",
        appDir + "/../../resources/Fig/logo.ico",
        QDir::currentPath() + "/resources/Fig/logo.ico",
    };

    for (const auto& path : searchPaths) {
        if (!QFile::exists(path))
            continue;

        // Try QImage (most reliable for .ico on Windows)
        QImage img(path);
        if (!img.isNull()) {
            logoPixmap_ = QPixmap::fromImage(img);
            return;
        }

        // Fallback: QIcon → QPixmap
        QIcon icon(path);
        QPixmap pix = icon.pixmap(256, 256);
        if (!pix.isNull()) {
            logoPixmap_ = pix;
            return;
        }
    }
    std::cerr << "[WelcomeScreen] Logo not found in any search path" << std::endl;
}

void WelcomeScreen::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    if (!logoPixmap_.isNull()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(0.18);

        int size = static_cast<int>(std::min(width(), height()) * 0.65);
        QPixmap scaled = logoPixmap_.scaled(
            size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
        painter.end();
    }
}

void WelcomeScreen::initUI()
{
    setStyleSheet("background-color: #f4f6f8;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top spacer to push content to vertical center
    layout->addStretch(1);

    // Title: "SpineX" at 110pt
    auto* titleLabel = new QLabel("SpineX");
    titleLabel->setStyleSheet(
        "color: #2c5f6f; background: transparent; letter-spacing: 4px;"
        "font-family: 'Segoe UI'; font-size: 110pt; font-weight: bold;");
    titleLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(titleLabel, 0, Qt::AlignHCenter);

    layout->addSpacing(10);

    // Accent divider (centered)
    auto* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedWidth(100);
    divider->setFixedHeight(3);
    divider->setStyleSheet(
        "background-color: #2c5f6f; border: none; max-height: 3px;");
    layout->addWidget(divider, 0, Qt::AlignHCenter);

    layout->addSpacing(24);

    // Subtitle
    auto* subtitleLabel = new QLabel("An expert spine surgery platform");
    subtitleLabel->setStyleSheet(
        "color: #6b7280; background: transparent;"
        "font-family: 'Segoe UI'; font-size: 26pt;");
    subtitleLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(subtitleLabel, 0, Qt::AlignHCenter);

    layout->addSpacing(10);

    // Hint
    auto* hintLabel = new QLabel("Use the  Modules  menu above to get started");
    hintLabel->setStyleSheet(
        "color: #9ca3af; background: transparent;"
        "font-family: 'Segoe UI'; font-size: 14pt;");
    hintLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(hintLabel, 0, Qt::AlignHCenter);

    // Push copyright to bottom
    layout->addStretch(1);

    // Copyright footer
    auto* copyrightLabel = new QLabel(
        QString::fromUtf8("\u00A9 2025\u20132026 \u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8 All Rights Reserved."));
    copyrightLabel->setStyleSheet(
        "color: #c0c4cc; background: transparent;"
        "font-family: 'Segoe UI'; font-size: 10pt;");
    copyrightLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(copyrightLabel, 0, Qt::AlignHCenter);

    layout->addSpacing(20);
}
