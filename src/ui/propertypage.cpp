#include "propertypage.h"
#include "propertyreport.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

PropertyPageWidgets setupPropertyPage(QWidget *page, const QString &heading,
    const QString &tooltip, const QString &copyCaption, const PropertyPageOptions &options)
{
    page->hide();
    auto *layout = new QVBoxLayout(page);
    auto *notice = plainLabel(heading, page);
    // Preserve QLabel's height-for-width flag when changing vertical sizing.
    auto policy = notice->sizePolicy();
    policy.setVerticalPolicy(options.headingPolicy);
    notice->setSizePolicy(policy);
    notice->setToolTip(tooltip);
    layout->addWidget(notice);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(options.frame);
    auto *content = new QWidget(scroll);
    auto *groups = new QVBoxLayout(content);
    groups->setSizeConstraint(options.contentConstraint);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    auto *copyButton = new QPushButton(copyCaption, page);
    layout->addWidget(copyButton);
    return {groups, copyButton};
}

// Only report content scrolls; tab actions stay above the dialog-wide footer.
ReportPageLayout reportPageLayout(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *contents = new QVBoxLayout(page);
    contents->setContentsMargins(0, 0, 0, 0);
    contents->setAlignment(Qt::AlignTop);
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    return {contents, actions};
}
