/**
 * ============================================================================
 * WelcomeScreen - 欢迎页面（对应 ui/welcome_screen.py）
 * ============================================================================
 */

#ifndef SPINEX_WELCOME_SCREEN_H
#define SPINEX_WELCOME_SCREEN_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>

class WelcomeScreen : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeScreen(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void loadLogo();
    void initUI();

    QPixmap logoPixmap_;
};

#endif // SPINEX_WELCOME_SCREEN_H
