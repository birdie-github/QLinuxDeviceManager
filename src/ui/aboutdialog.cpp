#include "aboutdialog.h"
#include "projectmetadata.h"
#ifdef QLDM_ENABLE_DONATION_BUTTON
#include "donatebutton.h"
#endif
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QVBoxLayout>

void showAboutDialog(QWidget *parent, QAction *donationAction)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QApplication::translate("MainWindow", "About QLinuxDeviceManager"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    const auto text = [](const char *source) { return QApplication::translate("MainWindow", source); };
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(dialog.fontMetrics().height());
    auto *header = new QHBoxLayout;
    auto *icon = new QLabel(&dialog);
    icon->setPixmap(QApplication::windowIcon().pixmap(QSize(64, 64), dialog.devicePixelRatioF()));
    header->addWidget(icon, 0, Qt::AlignTop);
    auto *identity = new QVBoxLayout;
    auto *title = new QLabel(QCoreApplication::applicationName(), &dialog);
    title->setTextFormat(Qt::PlainText);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0) titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    else titleFont.setPixelSize(qRound(titleFont.pixelSize() * 1.3));
    title->setFont(titleFont);
    identity->addWidget(title);
    identity->addWidget(new QLabel(text("Version %1 · Qt %2")
        .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())), &dialog));
    header->addLayout(identity, 1);
    layout->addLayout(header);

    const auto paragraph = [&dialog, layout](const QString &content, bool rich = false) {
        auto *label = new QLabel(content, &dialog);
        label->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
    };
    paragraph(text("A read-only Linux hardware viewer."));
    paragraph(QStringLiteral("<a href=\"%1\">%2</a>")
        .arg(QString::fromUtf8(QLDM_REPOSITORY).toHtmlEscaped(), text("Project website").toHtmlEscaped()), true);
    paragraph(text("Categories are application groupings, not native Linux device classes. "
                   "Device presence does not establish hardware health."));
    auto *credits = new QLabel(text("License and credits"), &dialog);
    QFont creditsFont = credits->font();
    creditsFont.setBold(true);
    credits->setFont(creditsFont);
    layout->addWidget(credits);
    paragraph(text("Application code is licensed under GNU GPL version 3; see LICENSE."));
    paragraph(text("Optional Windows 11 Color icons by %1. Copyright Icons8. "
                   "Icons8 assets are excluded from GPLv3. They may be included in this project and downstream builds; "
                   "independent extraction, redistribution or reuse requires a license from Icons8.")
        .toHtmlEscaped().arg(QStringLiteral("<a href=\"https://icons8.com/\">Icons8</a>")), true);

    auto *footer = new QHBoxLayout;
#ifdef QLDM_ENABLE_DONATION_BUTTON
    if (donationAction) {
        auto *donate = new DonateButton(&dialog);
        footer->addWidget(donate);
        QObject::connect(donate, &QPushButton::clicked, donationAction, &QAction::trigger);
    }
#else
    Q_UNUSED(donationAction);
#endif
    footer->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    footer->addWidget(buttons);
    layout->addLayout(footer);
    dialog.exec();
}
