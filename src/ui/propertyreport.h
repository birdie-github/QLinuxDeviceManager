#pragma once

#include <QStringList>
#include <QVector>

class QGridLayout;
class QLabel;
class QVBoxLayout;
class QWidget;

// Plain, selectable wrapped values and escaped source tooltips shared by pages.
QLabel *plainLabel(const QString &text, QWidget *parent);
QString propertyToolTip(const QString &source);

// A short-lived renderer for one rebuild. Layouts own the widgets; copy receives
// the same displayed headings/values and may also contain page-specific tables.
// Destroy this renderer before clearing the page or destroying copy.
class PropertyReport final {
public:
    PropertyReport(QVBoxLayout *groups, QStringList &copy);
    QGridLayout *group(const QString &title);
    void field(QGridLayout *form, const QString &caption, const QString &text,
               const QString &source = {});
    void alignCaptions();

private:
    QVBoxLayout *groups_;
    QStringList &copy_;
    QVector<QGridLayout *> forms_;
    QVector<QLabel *> captions_;
};
