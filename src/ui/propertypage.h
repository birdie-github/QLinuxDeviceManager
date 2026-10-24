#pragma once

#include <QFrame>
#include <QLayout>
#include <QSizePolicy>
#include <QString>

class QPushButton;
class QVBoxLayout;

// Preserve each existing page's layout while sharing its construction.
struct PropertyPageOptions {
    QFrame::Shape frame = QFrame::StyledPanel;
    QLayout::SizeConstraint contentConstraint = QLayout::SetMinAndMaxSize;
    QSizePolicy::Policy headingPolicy = QSizePolicy::Preferred;
};

struct PropertyPageWidgets {
    QWidget *page;
    QVBoxLayout *groups;
    QPushButton *copyButton;
};

// Widgets are owned by the returned page, which is parented to parent and
// initially hidden. The caller controls tab insertion and clipboard content.
PropertyPageWidgets createPropertyPage(QWidget *parent, const QString &heading,
    const QString &tooltip, const QString &copyCaption,
    const PropertyPageOptions &options = {});
