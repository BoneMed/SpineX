/**
 * ============================================================================
 * AppConfig - 应用配置和样式（实现）
 * 从 Python AppUtils/config.py 1:1 移植
 * ============================================================================
 */

#include "AppConfig.h"

AppConfig::AppConfig()
{
    initColors();
    initFileTypes();
    stylesheet_ = createStylesheet();
}

void AppConfig::initColors()
{
    colors_["bg"]               = "#f4f6f8";
    colors_["menu_bg"]          = "#ffffff";
    colors_["title"]            = "#2c5f6f";
    colors_["subtitle"]         = "#6b7280";
    colors_["accent"]           = "#2c5f6f";
    colors_["button_primary"]   = "#16a34a";
    colors_["button_secondary"] = "#3b82f6";
    colors_["button_danger"]    = "#dc2626";
}

void AppConfig::initFileTypes()
{
    fileTypes_["medical_images"] =
        "Medical Images (*.nii *.nii.gz *.dcm *.mha *.mhd);;All files (*.*)";
    fileTypes_["stl_files"] =
        "STL files (*.stl);;All files (*.*)";
    fileTypes_["project_files"] =
        "Project files (*.json);;All files (*.*)";
}

QString AppConfig::color(const QString& key) const
{
    return colors_.value(key, "#000000");
}

QString AppConfig::fileFilter(const QString& key) const
{
    return fileTypes_.value(key, "All files (*.*)");
}

QString AppConfig::createStylesheet() const
{
    return R"(
        /* ── Base ── */
        QWidget {
            color: #1a2332;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 10pt;
        }
        QMainWindow, QDialog, QFrame,
        QStackedWidget > QWidget {
            background-color: #f4f6f8;
        }
        QLabel {
            background-color: transparent;
            color: #1a2332;
        }

        /* ── GroupBox ── */
        QGroupBox {
            background-color: #ffffff;
            border: 1px solid #dde1e7;
            border-radius: 6px;
            margin-top: 16px;
            padding-top: 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 2px 8px;
            color: #6b7280;
            font-size: 8pt;
            font-weight: bold;
            background-color: #f4f6f8;
            border-radius: 3px;
        }

        /* ── Buttons ── */
        QPushButton {
            min-height: 36px;
            padding: 0 16px;
            border-radius: 6px;
            font-size: 10pt;
            font-weight: bold;
            border: none;
            color: white;
            background-color: #2c5f6f;
        }
        QPushButton:hover {
            background-color: #1e4a57;
        }
        QPushButton:disabled {
            background-color: #d1d5db;
            color: #9ca3af;
        }

        /* ── All dialog buttons: logo green + white text ── */
        QMessageBox QPushButton,
        QDialog QPushButton,
        QProgressDialog QPushButton,
        QInputDialog QPushButton,
        QDialogButtonBox QPushButton {
            background-color: #2c5f6f;
            color: white;
            border: none;
            min-width: 80px;
            min-height: 32px;
            padding: 0 16px;
            border-radius: 6px;
            font-size: 10pt;
            font-weight: bold;
        }
        QMessageBox QPushButton:hover,
        QDialog QPushButton:hover,
        QProgressDialog QPushButton:hover,
        QInputDialog QPushButton:hover,
        QDialogButtonBox QPushButton:hover {
            background-color: #1e4a57;
        }
        QMessageBox QPushButton:pressed,
        QDialog QPushButton:pressed,
        QProgressDialog QPushButton:pressed,
        QDialogButtonBox QPushButton:pressed {
            background-color: #163d4a;
        }

        /* ── Inputs ── */
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            padding: 5px 8px;
            border: 1px solid #dde1e7;
            border-radius: 4px;
            background-color: #ffffff;
            color: #1a2332;
            font-size: 10pt;
            min-height: 28px;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border: 1.5px solid #2c5f6f;
            outline: none;
        }
        QTextEdit {
            border: 1px solid #dde1e7;
            border-radius: 4px;
            background-color: #ffffff;
            color: #1a2332;
            font-size: 10pt;
            padding: 4px;
        }
        QTextEdit:focus {
            border: 1.5px solid #2c5f6f;
        }

        /* ── ComboBox dropdown ── */
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            border: 1px solid #dde1e7;
            border-radius: 4px;
            background: #ffffff;
            selection-background-color: #e8f0fe;
            selection-color: #1a2332;
        }

        /* ── Checkbox ── */
        QCheckBox {
            spacing: 6px;
            font-size: 10pt;
            color: #1a2332;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 2px solid #dde1e7;
            border-radius: 3px;
            background-color: #ffffff;
        }
        QCheckBox::indicator:checked {
            background-color: #2c5f6f;
            border-color: #2c5f6f;
        }
        QCheckBox::indicator:hover {
            border-color: #2c5f6f;
        }

        /* ── RadioButton ── */
        QRadioButton {
            spacing: 6px;
            font-size: 10pt;
        }
        QRadioButton::indicator {
            width: 16px;
            height: 16px;
            border: 2px solid #dde1e7;
            border-radius: 8px;
            background-color: #ffffff;
        }
        QRadioButton::indicator:checked {
            background-color: #2c5f6f;
            border-color: #2c5f6f;
        }

        /* ── Scrollbar ── */
        QScrollBar:vertical {
            border: none;
            background-color: #f4f6f8;
            width: 6px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background-color: #ced4da;
            border-radius: 3px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #9ca3af;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            border: none;
            background-color: #f4f6f8;
            height: 6px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background-color: #ced4da;
            border-radius: 3px;
            min-width: 20px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: #9ca3af;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }

        /* ── Slider ── */
        QSlider::groove:horizontal {
            background: #e9ecef;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #2c5f6f;
            width: 14px;
            height: 14px;
            border-radius: 7px;
            margin: -5px 0;
        }
        QSlider::sub-page:horizontal {
            background: #2c5f6f;
            border-radius: 2px;
        }

        /* ── Progress bar ── */
        QProgressBar {
            border: 1px solid #dde1e7;
            border-radius: 4px;
            text-align: center;
            background-color: #f4f6f8;
            color: #1a2332;
            height: 22px;
            font-size: 9pt;
        }
        QProgressBar::chunk {
            background-color: #2c5f6f;
            border-radius: 3px;
        }

        /* ── Tables ── */
        QTableWidget, QTreeWidget {
            border: 1px solid #dde1e7;
            border-radius: 4px;
            background-color: #ffffff;
            gridline-color: #edf0f5;
            selection-background-color: #e8f0fe;
            selection-color: #1a2332;
            alternate-background-color: #f9fafb;
        }
        QTableWidget::item, QTreeWidget::item {
            padding: 4px 8px;
        }
        QHeaderView::section {
            background-color: #f4f6f8;
            padding: 6px 10px;
            border: none;
            border-right: 1px solid #dde1e7;
            border-bottom: 1px solid #dde1e7;
            font-weight: bold;
            font-size: 9pt;
            color: #6b7280;
        }

        /* ── Tabs ── */
        QTabWidget::pane {
            border: 1px solid #dde1e7;
            border-radius: 4px;
            background-color: #ffffff;
        }
        QTabBar::tab {
            padding: 8px 18px;
            background-color: #f4f6f8;
            border: 1px solid #dde1e7;
            border-bottom: none;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            font-size: 10pt;
            color: #6b7280;
        }
        QTabBar::tab:selected {
            background-color: #ffffff;
            color: #2c5f6f;
            font-weight: bold;
        }
        QTabBar::tab:hover {
            background-color: #e8f0fe;
            color: #1a2332;
        }

        /* ── ScrollArea ── */
        QScrollArea {
            border: none;
            background-color: transparent;
        }
        QScrollArea > QWidget > QWidget {
            background-color: transparent;
        }
    )";
}
