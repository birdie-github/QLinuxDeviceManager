#pragma once
#include <QPushButton>

// Reusable Qt Widgets control: add this header/source to a project and connect
// clicked() to the host's donation action. No URL or settings are owned here.
// QPushButton provides keyboard and accessibility behavior.
class DonateButton final : public QPushButton {
public:
    explicit DonateButton(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent *event) override;
};
