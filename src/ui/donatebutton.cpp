#include "donatebutton.h"
#include <QCoreApplication>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFocusRect>

namespace {
QString caption()
{
    return QCoreApplication::translate("DonateButton", "Donate with ");
}
QFont brandFont(QFont font)
{
    font.setBold(true);
    font.setItalic(true);
    return font;
}
}

DonateButton::DonateButton(QWidget *parent) : QPushButton(parent)
{
    setText(caption() + QStringLiteral("PayPal"));
    setToolTip(QCoreApplication::translate("DonateButton", "Support development via PayPal"));
    setAutoDefault(false);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
}

QSize DonateButton::sizeHint() const
{
    const QFontMetrics brand(brandFont(font()));
    return QSize(fontMetrics().horizontalAdvance(caption())
        + brand.horizontalAdvance(QStringLiteral("Pay"))
        + brand.horizontalAdvance(QStringLiteral("Pal")) + 24,
        qMax(fontMetrics().height(), brand.height()) + 12);
}

void DonateButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) painter.setOpacity(0.5);
    const QRectF pill = QRectF(rect()).adjusted(1, 1, -1, -1);
    QLinearGradient gradient(pill.topLeft(), pill.bottomLeft());
    const auto shade = [this](QColor color) {
        if (isDown()) return color.darker(110);
        return underMouse() ? color.lighter(105) : color;
    };
    gradient.setColorAt(0, shade(QColor("#ffcc33")));
    gradient.setColorAt(0.5, shade(QColor("#ffb700")));
    gradient.setColorAt(1, shade(QColor("#e6a200")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);

    const QFont brand = brandFont(font());
    const QFontMetrics brandMetrics(brand);
    const int prefixWidth = fontMetrics().horizontalAdvance(caption());
    const int payWidth = brandMetrics.horizontalAdvance(QStringLiteral("Pay"));
    const int textWidth = prefixWidth + payWidth + brandMetrics.horizontalAdvance(QStringLiteral("Pal"));
    int x = (width() - textWidth) / 2;
    const int baseline = (height() - fontMetrics().height()) / 2 + fontMetrics().ascent();
    painter.setFont(font());
    painter.setPen(QColor("#1a1a1a"));
    painter.drawText(x, baseline, caption());
    x += prefixWidth;
    painter.setFont(brand);
    painter.setPen(QColor("#003087"));
    painter.drawText(x, baseline, QStringLiteral("Pay"));
    painter.setPen(QColor("#009cde"));
    painter.drawText(x + payWidth, baseline, QStringLiteral("Pal"));

    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = rect().adjusted(5, 5, -5, -5);
        focus.backgroundColor = QColor("#ffb700");
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
    }
}
