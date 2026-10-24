#include "propertypage.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

PropertyPageWidgets createPropertyPage(QWidget *parent, const QString &heading,
    const QString &tooltip, const QString &copyCaption, const PropertyPageOptions &options)
{
    return setupPropertyPage(new QWidget(parent), heading, tooltip, copyCaption, options);
}

PropertyPageWidgets setupPropertyPage(QWidget *page, const QString &heading,
    const QString &tooltip, const QString &copyCaption, const PropertyPageOptions &options)
{
    page->hide();
    auto *layout = new QVBoxLayout(page);
    auto *notice = new QLabel(heading, page);
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    notice->setFocusPolicy(Qt::StrongFocus);
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
    return {page, groups, copyButton};
}
