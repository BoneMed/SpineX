/**
 * ============================================================================
 * LicenseDialog - 登录/注册对话框（实现）
 * ============================================================================
 */

#include "LicenseDialog.h"
#include "app/LicenseManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QRegularExpression>
#include <QDesktopServices>
#include <iostream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>

// ---- 共用样式 ----
static const char* INPUT_STYLE =
    "QLineEdit {"
    "  font-size: 12pt; padding: 10px 14px;"
    "  border: 2px solid #dde1e7; border-radius: 8px;"
    "  background-color: #ffffff; color: #1a2332;"
    "}"
    "QLineEdit:focus { border-color: #2c5f6f; }";

static const char* BTN_PRIMARY =
    "QPushButton {"
    "  background-color: #2c5f6f; color: white;"
    "  font-size: 12pt; font-weight: bold;"
    "  padding: 10px 28px; border: none; border-radius: 8px;"
    "}"
    "QPushButton:hover { background-color: #1e4a57; }"
    "QPushButton:pressed { background-color: #163d4a; }";

static const char* BTN_SECONDARY =
    "QPushButton {"
    "  background-color: #d1d5db; color: #374151;"
    "  font-size: 12pt; font-weight: bold;"
    "  padding: 10px 28px; border: none; border-radius: 8px;"
    "}"
    "QPushButton:hover { background-color: #b0b5bd; }";

static const char* LINK_STYLE =
    "QPushButton {"
    "  background: transparent; color: #2c5f6f;"
    "  font-size: 10pt; border: none; text-decoration: underline;"
    "}"
    "QPushButton:hover { color: #1e4a57; }";

// ---- 构造 ----

LicenseDialog::LicenseDialog(QWidget* parent)
    : QDialog(parent)
    , activated_(false)
{
    setWindowTitle("SpineX");
    setMinimumSize(500, 520);
    resize(500, 520);
    setStyleSheet("background-color: #f4f6f8;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    pages_ = new QStackedWidget();
    mainLayout->addWidget(pages_);

    // ========== Page 0: Login ==========
    auto* loginPage = new QWidget();
    auto* ll = new QVBoxLayout(loginPage);
    ll->setSpacing(10);
    ll->setContentsMargins(40, 30, 40, 20);

    auto* title1 = new QLabel("SpineX");
    title1->setStyleSheet("color: #2c5f6f; font-size: 36pt; font-weight: bold;"
                           "font-family: 'Segoe UI'; background: transparent;");
    title1->setAlignment(Qt::AlignCenter);
    ll->addWidget(title1);

    auto* sub1 = new QLabel("Sign in to your account");
    sub1->setStyleSheet("color: #6b7280; font-size: 12pt; background: transparent;");
    sub1->setAlignment(Qt::AlignCenter);
    ll->addWidget(sub1);

    ll->addSpacing(12);

    loginEmail_ = new QLineEdit();
    loginEmail_->setPlaceholderText("Email");
    loginEmail_->setStyleSheet(INPUT_STYLE);
    ll->addWidget(loginEmail_);

    loginPassword_ = new QLineEdit();
    loginPassword_->setPlaceholderText("Password");
    loginPassword_->setEchoMode(QLineEdit::Password);
    loginPassword_->setStyleSheet(INPUT_STYLE);
    ll->addWidget(loginPassword_);

    loginStatus_ = new QLabel("");
    loginStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
    loginStatus_->setAlignment(Qt::AlignCenter);
    loginStatus_->setWordWrap(true);
    ll->addWidget(loginStatus_);

    auto* lb = new QHBoxLayout();
    auto* loginBtn = new QPushButton("Sign In");
    loginBtn->setStyleSheet(BTN_PRIMARY);
    connect(loginBtn, &QPushButton::clicked, this, &LicenseDialog::onLogin);
    auto* exitBtn1 = new QPushButton("Exit");
    exitBtn1->setStyleSheet(BTN_SECONDARY);
    connect(exitBtn1, &QPushButton::clicked, this, &QDialog::reject);
    lb->addStretch(); lb->addWidget(loginBtn); lb->addWidget(exitBtn1); lb->addStretch();
    ll->addLayout(lb);

    ll->addSpacing(8);
    auto* toRegBtn = new QPushButton("Don't have an account? Register");
    toRegBtn->setStyleSheet(LINK_STYLE);
    connect(toRegBtn, &QPushButton::clicked, this, &LicenseDialog::showRegisterPage);
    ll->addWidget(toRegBtn, 0, Qt::AlignCenter);

    connect(loginPassword_, &QLineEdit::returnPressed, this, &LicenseDialog::onLogin);
    connect(loginEmail_, &QLineEdit::returnPressed, [this]() { loginPassword_->setFocus(); });

    pages_->addWidget(loginPage);

    // ========== Page 1: Register ==========
    auto* regPage = new QWidget();
    auto* rl = new QVBoxLayout(regPage);
    rl->setSpacing(10);
    rl->setContentsMargins(40, 30, 40, 20);

    auto* title2 = new QLabel("SpineX");
    title2->setStyleSheet("color: #2c5f6f; font-size: 36pt; font-weight: bold;"
                           "font-family: 'Segoe UI'; background: transparent;");
    title2->setAlignment(Qt::AlignCenter);
    rl->addWidget(title2);

    auto* sub2 = new QLabel("Create a new account");
    sub2->setStyleSheet("color: #6b7280; font-size: 12pt; background: transparent;");
    sub2->setAlignment(Qt::AlignCenter);
    rl->addWidget(sub2);

    rl->addSpacing(8);

    regName_ = new QLineEdit();
    regName_->setPlaceholderText("Full Name");
    regName_->setStyleSheet(INPUT_STYLE);
    rl->addWidget(regName_);

    regEmail_ = new QLineEdit();
    regEmail_->setPlaceholderText("Email");
    regEmail_->setStyleSheet(INPUT_STYLE);
    rl->addWidget(regEmail_);

    regPassword_ = new QLineEdit();
    regPassword_->setPlaceholderText("Password (min 8 characters)");
    regPassword_->setEchoMode(QLineEdit::Password);
    regPassword_->setStyleSheet(INPUT_STYLE);
    rl->addWidget(regPassword_);

    regStatus_ = new QLabel("");
    regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
    regStatus_->setAlignment(Qt::AlignCenter);
    regStatus_->setWordWrap(true);
    rl->addWidget(regStatus_);

    auto* rb = new QHBoxLayout();
    auto* regBtn = new QPushButton("Register");
    regBtn->setStyleSheet(BTN_PRIMARY);
    connect(regBtn, &QPushButton::clicked, this, &LicenseDialog::onRegister);
    auto* exitBtn2 = new QPushButton("Exit");
    exitBtn2->setStyleSheet(BTN_SECONDARY);
    connect(exitBtn2, &QPushButton::clicked, this, &QDialog::reject);
    rb->addStretch(); rb->addWidget(regBtn); rb->addWidget(exitBtn2); rb->addStretch();
    rl->addLayout(rb);

    rl->addSpacing(8);
    auto* toLoginBtn = new QPushButton("Already have an account? Sign In");
    toLoginBtn->setStyleSheet(LINK_STYLE);
    connect(toLoginBtn, &QPushButton::clicked, this, &LicenseDialog::showLoginPage);
    rl->addWidget(toLoginBtn, 0, Qt::AlignCenter);

    connect(regPassword_, &QLineEdit::returnPressed, this, &LicenseDialog::onRegister);

    pages_->addWidget(regPage);

    // ========== Page 2: Pending Approval ==========
    auto* pendingPage = new QWidget();
    auto* pl = new QVBoxLayout(pendingPage);
    pl->setSpacing(16);
    pl->setContentsMargins(40, 50, 40, 30);

    auto* title3 = new QLabel("SpineX");
    title3->setStyleSheet("color: #2c5f6f; font-size: 36pt; font-weight: bold;"
                           "font-family: 'Segoe UI'; background: transparent;");
    title3->setAlignment(Qt::AlignCenter);
    pl->addWidget(title3);

    pl->addSpacing(10);

    auto* pendingIcon = new QLabel(QString::fromUtf8("\u23F3"));  // ⏳
    pendingIcon->setStyleSheet("font-size: 48pt; background: transparent;");
    pendingIcon->setAlignment(Qt::AlignCenter);
    pl->addWidget(pendingIcon);

    pendingStatusLabel_ = new QLabel(
        "Payment submitted.\n\n"
        "Waiting for verification...\n"
        "This page will update automatically.");
    pendingStatusLabel_->setStyleSheet("color: #374151; font-size: 12pt; background: transparent;"
                               "font-family: 'Segoe UI';");
    pendingStatusLabel_->setAlignment(Qt::AlignCenter);
    pendingStatusLabel_->setWordWrap(true);
    pl->addWidget(pendingStatusLabel_);

    pl->addStretch();

    pendingSignInBtn_ = new QPushButton("Sign In");
    pendingSignInBtn_->setStyleSheet(BTN_PRIMARY);
    pendingSignInBtn_->setVisible(false);  // 默认隐藏，通过后才显示
    connect(pendingSignInBtn_, &QPushButton::clicked, this, &LicenseDialog::showLoginPage);

    auto* pb = new QHBoxLayout();
    pb->addStretch();
    pb->addWidget(pendingSignInBtn_);
    auto* exitBtn3 = new QPushButton("Exit");
    exitBtn3->setStyleSheet(BTN_SECONDARY);
    connect(exitBtn3, &QPushButton::clicked, this, &QDialog::reject);
    pb->addWidget(exitBtn3);
    pb->addStretch();
    pl->addLayout(pb);

    // 每 5 秒检查一次是否已激活
    pendingCheckTimer_ = new QTimer(this);
    connect(pendingCheckTimer_, &QTimer::timeout, this, &LicenseDialog::checkActivation);
    // timer 在 showPendingPage() 时启动

    pages_->addWidget(pendingPage);

    // ========== Page 3: Choose Plan ==========
    auto* planPage = new QWidget();
    auto* nl = new QVBoxLayout(planPage);
    nl->setSpacing(8);
    nl->setContentsMargins(20, 20, 20, 15);

    auto* title4 = new QLabel("Choose Your Plan");
    title4->setStyleSheet("color: #2c5f6f; font-size: 22pt; font-weight: bold;"
                           "font-family: 'Segoe UI'; background: transparent;");
    title4->setAlignment(Qt::AlignCenter);
    nl->addWidget(title4);

    auto* subTitle = new QLabel("Select the plan that fits your research needs");
    subTitle->setStyleSheet("color: #6b7280; font-size: 10pt; background: transparent;");
    subTitle->setAlignment(Qt::AlignCenter);
    nl->addWidget(subTitle);

    nl->addSpacing(6);

    // ---- Two plan cards side by side ----
    auto* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(12);

    // Card style
    static const char* CARD_STYLE =
        "QFrame { background: white; border: 2px solid %1; border-radius: 12px; }";
    static const char* CARD_TITLE =
        "font-size: 16pt; font-weight: bold; color: %1; background: transparent;";
    static const char* CARD_PRICE =
        "font-size: 24pt; font-weight: bold; color: #1a2332; background: transparent;";
    static const char* CARD_PERIOD =
        "font-size: 9pt; color: #9ca3af; background: transparent;";
    static const char* CARD_FEATURE =
        "font-size: 9pt; color: #374151; background: transparent; padding: 1px 0;";
    static const char* CARD_FEATURE_NO =
        "font-size: 9pt; color: #d1d5db; background: transparent; padding: 1px 0;";

    // ---- Community Plan ----
    auto* communityCard = new QFrame();
    communityCard->setStyleSheet(QString(CARD_STYLE).arg("#2c5f6f"));
    communityCard->setFixedWidth(260);
    auto* cl = new QVBoxLayout(communityCard);
    cl->setSpacing(4);
    cl->setContentsMargins(14, 14, 14, 14);

    auto* cTitle = new QLabel("Community");
    cTitle->setStyleSheet(QString(CARD_TITLE).arg("#2c5f6f"));
    cTitle->setAlignment(Qt::AlignCenter);
    cl->addWidget(cTitle);

    auto* cPrice = new QLabel(QString::fromUtf8("\u00A599"));
    cPrice->setStyleSheet(CARD_PRICE);
    cPrice->setAlignment(Qt::AlignCenter);
    cl->addWidget(cPrice);
    auto* cPeriod = new QLabel("/month");
    cPeriod->setStyleSheet(CARD_PERIOD);
    cPeriod->setAlignment(Qt::AlignCenter);
    cl->addWidget(cPeriod);

    cl->addSpacing(6);

    QStringList communityFeatures = {
        QString::fromUtf8("\u2713  CT Segmentation (DL)"),
        QString::fromUtf8("\u2713  BMD Analysis"),
        QString::fromUtf8("\u2713  3D Visualization"),
        QString::fromUtf8("\u2713  Cloud History & Alerts"),
        QString::fromUtf8("\u2713  Export VTU"),
        QString::fromUtf8("\u2713  3 Devices"),
        QString::fromUtf8("\u2713  Contribute to Research"),
    };
    for (const auto& f : communityFeatures) {
        auto* fl = new QLabel(f);
        fl->setStyleSheet(CARD_FEATURE);
        cl->addWidget(fl);
    }
    auto* cNote = new QLabel("Anonymized CT data shared\nfor model improvement");
    cNote->setStyleSheet("font-size: 8pt; color: #6b7280; background: transparent;"
                          "font-style: italic; padding-top: 4px;");
    cNote->setWordWrap(true);
    cl->addWidget(cNote);

    cl->addStretch();

    selectedTier_ = "community";  // default selection

    auto* cBtn = new QPushButton("Select Community");
    cBtn->setStyleSheet(BTN_PRIMARY);
    cBtn->setFocusPolicy(Qt::NoFocus);  // 防止 Enter 键误触
    connect(cBtn, &QPushButton::clicked, [this]() {
        selectedTier_ = "community";
        onRequestLicense();
    });
    cl->addWidget(cBtn);

    cardsLayout->addWidget(communityCard);

    // ---- Professional Plan ----
    auto* proCard = new QFrame();
    proCard->setStyleSheet(QString(CARD_STYLE).arg("#f59e0b"));
    proCard->setFixedWidth(260);
    auto* pl2 = new QVBoxLayout(proCard);
    pl2->setSpacing(4);
    pl2->setContentsMargins(14, 14, 14, 14);

    auto* pBadge = new QLabel("RECOMMENDED");
    pBadge->setStyleSheet("font-size: 8pt; font-weight: bold; color: white;"
                           "background: #f59e0b; border-radius: 4px; padding: 2px 8px;");
    pBadge->setAlignment(Qt::AlignCenter);
    pl2->addWidget(pBadge);

    auto* pTitle = new QLabel("Professional");
    pTitle->setStyleSheet(QString(CARD_TITLE).arg("#f59e0b"));
    pTitle->setAlignment(Qt::AlignCenter);
    pl2->addWidget(pTitle);

    auto* pPrice = new QLabel(QString::fromUtf8("\u00A5299"));
    pPrice->setStyleSheet(CARD_PRICE);
    pPrice->setAlignment(Qt::AlignCenter);
    pl2->addWidget(pPrice);
    auto* pPeriod = new QLabel("/month");
    pPeriod->setStyleSheet(CARD_PERIOD);
    pPeriod->setAlignment(Qt::AlignCenter);
    pl2->addWidget(pPeriod);

    pl2->addSpacing(6);

    QStringList proFeatures = {
        QString::fromUtf8("\u2713  CT Segmentation (DL)"),
        QString::fromUtf8("\u2713  BMD Analysis"),
        QString::fromUtf8("\u2713  3D Visualization"),
        QString::fromUtf8("\u2713  Cloud History & Alerts"),
        QString::fromUtf8("\u2713  Export VTU"),
        QString::fromUtf8("\u2713  3 Devices"),
        QString::fromUtf8("\u2713  Full Data Privacy"),
    };
    for (const auto& f : proFeatures) {
        auto* fl = new QLabel(f);
        fl->setStyleSheet(CARD_FEATURE);
        pl2->addWidget(fl);
    }
    auto* pNote = new QLabel("Your data stays 100% private\nNever shared or accessed");
    pNote->setStyleSheet("font-size: 8pt; color: #6b7280; background: transparent;"
                          "font-style: italic; padding-top: 4px;");
    pNote->setWordWrap(true);
    pl2->addWidget(pNote);

    pl2->addStretch();

    auto* pBtn = new QPushButton("Select Professional");
    pBtn->setFocusPolicy(Qt::NoFocus);
    pBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #f59e0b; color: white;"
        "  font-size: 11pt; font-weight: bold;"
        "  padding: 8px 16px; border: none; border-radius: 8px;"
        "}"
        "QPushButton:hover { background-color: #d97706; }");
    connect(pBtn, &QPushButton::clicked, [this]() {
        selectedTier_ = "professional";
        onRequestLicense();
    });
    pl2->addWidget(pBtn);

    cardsLayout->addWidget(proCard);

    nl->addLayout(cardsLayout);

    nl->addSpacing(4);

    noLicStatus_ = new QLabel("");
    noLicStatus_->setStyleSheet("color: #6b7280; font-size: 10pt; background: transparent;");
    noLicStatus_->setAlignment(Qt::AlignCenter);
    noLicStatus_->setWordWrap(true);
    nl->addWidget(noLicStatus_);

    auto* nb = new QHBoxLayout();
    auto* backBtn4 = new QPushButton("Back to Sign In");
    backBtn4->setStyleSheet(BTN_SECONDARY);
    connect(backBtn4, &QPushButton::clicked, this, &LicenseDialog::showLoginPage);
    auto* exitBtn4 = new QPushButton("Exit");
    exitBtn4->setStyleSheet(BTN_SECONDARY);
    connect(exitBtn4, &QPushButton::clicked, this, &QDialog::reject);
    nb->addStretch(); nb->addWidget(backBtn4); nb->addWidget(exitBtn4); nb->addStretch();
    nl->addLayout(nb);

    pages_->addWidget(planPage);

    // 默认显示登录页
    pages_->setCurrentIndex(0);
}

void LicenseDialog::showLoginPage() { resize(500, 520); pages_->setCurrentIndex(0); }
void LicenseDialog::showRegisterPage() { resize(500, 520); pages_->setCurrentIndex(1); }
void LicenseDialog::showPendingPage() {
    resize(500, 420);
    pages_->setCurrentIndex(2);
    // 重置为等待状态
    pendingStatusLabel_->setText("Payment submitted.\n\nWaiting for verification...\nThis page will update automatically.");
    pendingStatusLabel_->setStyleSheet("color: #374151; font-size: 12pt; background: transparent; font-family: 'Segoe UI';");
    pendingSignInBtn_->setVisible(false);
    if (pendingCheckTimer_ && !pendingCheckTimer_->isActive())
        pendingCheckTimer_->start(5000);
}
void LicenseDialog::showNoLicensePage() { resize(620, 620); pages_->setCurrentIndex(3); }

// ---- 登录 ----

void LicenseDialog::onLogin()
{
    QString email = loginEmail_->text().trimmed();
    QString password = loginPassword_->text();
    if (email.isEmpty() || password.isEmpty()) {
        loginStatus_->setText("Please enter email and password.");
        return;
    }
    QRegularExpression emailRegex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    if (!emailRegex.match(email).hasMatch()) {
        loginStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        loginStatus_->setText("Please enter a valid email address.");
        return;
    }

    loginStatus_->setStyleSheet("color: #6b7280; font-size: 10pt; background: transparent;");
    loginStatus_->setText("Connecting...");
    QApplication::processEvents();

    auto& mgr = LicenseManager::instance();
    auto result = mgr.login(email, password);

    switch (result) {
    case LicenseManager::LoginOK:
        activated_ = true;
        accept();
        break;
    case LicenseManager::LoginNoLicense:
        showNoLicensePage();
        break;
    case LicenseManager::LoginPending:
        showPendingPage();
        break;
    case LicenseManager::LoginFailed:
    default:
        loginStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        loginStatus_->setText("Invalid email or password.");
        break;
    }
}

// ---- 注册 ----

void LicenseDialog::onRegister()
{
    QString name = regName_->text().trimmed();
    QString email = regEmail_->text().trimmed();
    QString password = regPassword_->text();

    if (name.isEmpty() || email.isEmpty() || password.isEmpty()) {
        regStatus_->setText("Please fill in all fields.");
        return;
    }
    // 邮箱格式验证
    QRegularExpression emailRegex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    if (!emailRegex.match(email).hasMatch()) {
        regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        regStatus_->setText("Please enter a valid email address.");
        return;
    }
    if (password.length() < 10) {
        regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        regStatus_->setText("Password must be at least 10 characters.");
        return;
    }
    {
        bool hasUpper = false, hasLower = false, hasSymbol = false;
        for (QChar c : password) {
            if (c.isUpper()) hasUpper = true;
            else if (c.isLower()) hasLower = true;
            else if (!c.isDigit()) hasSymbol = true;
        }
        if (!hasUpper || !hasLower || !hasSymbol) {
            regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
            regStatus_->setText("Password must contain at least one\nuppercase, one lowercase, and one symbol.");
            return;
        }
    }

    regStatus_->setStyleSheet("color: #6b7280; font-size: 10pt; background: transparent;");
    regStatus_->setText("Registering...");
    QApplication::processEvents();

    // 调用注册 API
    QJsonObject body;
    body["email"] = email;
    body["password"] = password;
    body["name"] = name;

    QNetworkAccessManager netMgr;
    QNetworkRequest req(QUrl("https://www.spinex.cn/api/auth/register"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = netMgr.post(req, QJsonDocument(body).toJson());

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray respData = reply->readAll();
    QJsonObject resp = QJsonDocument::fromJson(respData).object();
    reply->deleteLater();

    if (status == 201) {
        // 注册成功 → 记住邮箱和密码 → 跳到选择 Plan 页面
        registeredEmail_ = email;
        registeredPassword_ = password;
        showNoLicensePage();
        return;  // 明确 return，不执行后续代码
    } else if (status == 409) {
        regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        regStatus_->setText("This email is already registered.");
    } else {
        regStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        QString detail = resp.value("detail").toString();
        regStatus_->setText(detail.isEmpty() ? "Registration failed." : detail);
    }
}

// ---- 轮询检查是否已激活 ----

void LicenseDialog::checkActivation()
{
    auto& mgr = LicenseManager::instance();
    QString email = mgr.userEmail();
    if (email.isEmpty()) email = registeredEmail_;
    if (email.isEmpty()) return;

    // 尝试登录看是否已有 License
    QString password = loginPassword_->text();
    if (password.isEmpty()) password = registeredPassword_;
    if (password.isEmpty()) return;
    auto result = mgr.login(email, password);
    if (result == LicenseManager::LoginOK) {
        // 激活成功！
        pendingCheckTimer_->stop();
        pendingStatusLabel_->setText(
            "License activated successfully!\n\n"
            "Click Sign In to start using SpineX.");
        pendingStatusLabel_->setStyleSheet(
            "color: #10b981; font-size: 14pt; font-weight: bold;"
            "background: transparent; font-family: 'Segoe UI';");
        pendingSignInBtn_->setVisible(true);

        // 找到⏳图标改成✅
        auto* parent = pendingStatusLabel_->parentWidget();
        if (parent) {
            for (auto* child : parent->findChildren<QLabel*>()) {
                if (child->text().contains(QString::fromUtf8("\u23F3"))) {
                    child->setText(QString::fromUtf8("\u2705"));
                    break;
                }
            }
        }
    }
}

// ---- 申请新 License ----

void LicenseDialog::onRequestLicense()
{
    auto& mgr = LicenseManager::instance();
    QString email = mgr.userEmail();
    if (email.isEmpty()) email = registeredEmail_;
    if (email.isEmpty()) {
        noLicStatus_->setText("Please sign in or register first.");
        return;
    }

    QString tier = selectedTier_.isEmpty() ? "community" : selectedTier_;

    noLicStatus_->setStyleSheet("color: #6b7280; font-size: 10pt; background: transparent;");
    noLicStatus_->setText("Submitting request...");
    QApplication::processEvents();

    // 调用公开的 license request API（不需要 token）
    QJsonObject body;
    body["email"] = email;
    body["tier"] = tier;

    QNetworkAccessManager netMgr;
    QNetworkRequest req(QUrl("https://www.spinex.cn/api/license/request-plan"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 如果有 token 就带上，没有也能发
    QString token = mgr.storedToken();
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    QNetworkReply* reply = netMgr.post(req, QJsonDocument(body).toJson());

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status >= 200 && status < 300) {
        // 打开浏览器跳转到支付页面
        QString payUrl = QString("https://www.spinex.cn/pay?email=%1&tier=%2")
            .arg(email, tier);
        QDesktopServices::openUrl(QUrl(payUrl));

        // 跳到等待审批页面
        showPendingPage();
    } else {
        noLicStatus_->setStyleSheet("color: #ef4444; font-size: 10pt; background: transparent;");
        noLicStatus_->setText("Request failed. Please try again later.");
    }
}
