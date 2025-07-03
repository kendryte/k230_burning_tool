#include "AnimatedButton.h"
#include <QGraphicsColorizeEffect>
#include <QPainter>
#include <QPropertyAnimation>

AnimatedButton::AnimatedButton(QWidget *parent) : QPushButton(parent) 
{
    // Initialize animation system
    QGraphicsColorizeEffect *effect = new QGraphicsColorizeEffect(this);
    effect->setStrength(0); // Start with no effect
    setGraphicsEffect(effect);

    animate = new QPropertyAnimation(effect, "color");
    animate->setDuration(2000);
    effect->setColor(QColor(240, 50, 50, 0));
    animate->setKeyValueAt(0, QColor(240, 50, 50, 50));
    animate->setKeyValueAt(0.5, QColor(240, 50, 50, 255));
    animate->setKeyValueAt(1, QColor(240, 50, 50, 50));
    animate->setEasingCurve(QEasingCurve::InOutCubic);
    animate->setLoopCount(-1);

    // Ensure proper text rendering
    setStyleSheet(
        "AnimatedButton {"
        "    text-align: center;"
        "    padding: 2px;"
        "    spacing: 5px;"
        "}"
    );

    connect(this, &AnimatedButton::clicked, [=](bool enabled) {
        if (enabled) {
            animate->start();
        } else {
            animate->stop();
            effect->setColor(QColor(240, 50, 50, 0));
        }
    });
}

void AnimatedButton::paintEvent(QPaintEvent *event)
{
    // Let QPushButton handle basic painting first
    QPushButton::paintEvent(event);
    
    // Then apply our effects
    if (isChecked()) {
        QPainter painter(this);
        painter.setOpacity(0.3); // Subtle effect
        painter.fillRect(rect(), QColor(240, 50, 50, 50));
    }
}

AnimatedButton::~AnimatedButton() 
{
    delete animate;
}