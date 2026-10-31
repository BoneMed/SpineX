/**
 * ============================================================================
 * ModuleBase - 模块抽象基类（实现）
 * ============================================================================
 */

#include "ModuleBase.h"

ModuleBase::ModuleBase(const QString& name, QWidget* parent)
    : QWidget(parent)
    , name_(name)
    , app_(nullptr)
{
}

ModuleBase::~ModuleBase() = default;

QString ModuleBase::getName() const { return name_; }

void ModuleBase::refreshFromProject()
{
    // Default: no-op
}

void ModuleBase::setApp(SpineXApp* app)
{
    app_ = app;
}

// Static: create module header matching Python ui_utils._make_module_header
QWidget* ModuleBase::makeModuleHeader(const QString& titleText)
{
    auto* wrapper = new QFrame();
    wrapper->setStyleSheet("background-color: #ffffff; border: none;");
    wrapper->setFixedHeight(48);

    auto* h = new QHBoxLayout(wrapper);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(10);

    auto* bar = new QFrame();
    bar->setFixedWidth(3);
    bar->setFixedHeight(28);
    bar->setStyleSheet("background-color: #2c5f6f; border-radius: 1px;");

    auto* lbl = new QLabel(titleText);
    lbl->setStyleSheet(
        "font-size: 13pt; font-weight: bold; color: #1a2332; background: transparent;");

    h->addWidget(bar);
    h->addWidget(lbl);
    h->addStretch();

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #dde1e7; background-color: #dde1e7; max-height: 1px;");

    auto* outer = new QWidget();
    outer->setStyleSheet("background-color: #ffffff;");
    auto* v = new QVBoxLayout(outer);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(wrapper);
    v->addWidget(sep);

    return outer;
}
