#pragma once

#include <QPushButton>
#include <QPaintEvent>

class AnimatedButton : public QPushButton {
	class QPropertyAnimation *animate;
	class QGraphicsColorizeEffect *effect;

  public:
	AnimatedButton(QWidget *parent = nullptr);
	~AnimatedButton();
  protected:
    void paintEvent(QPaintEvent *event) override;  // Add this declaration
};
