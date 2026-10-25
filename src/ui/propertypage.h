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
    QVBoxLayout *groups;
    QPushButton *copyButton;
};

// Call once on a widget without a layout. Children are owned by page.
// The page starts hidden; the caller controls tab insertion and clipboard content.
PropertyPageWidgets setupPropertyPage(QWidget *page, const QString &heading,
    const QString &tooltip, const QString &copyCaption,
    const PropertyPageOptions &options = {});

class QHBoxLayout;
struct ReportPageLayout {
    QVBoxLayout *contents;
    QHBoxLayout *actions;
};
ReportPageLayout reportPageLayout(QWidget *parent);
