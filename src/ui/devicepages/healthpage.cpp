#include "healthpage.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QString selectedHealthText(const QVector<StorageDisplayRow> &rows, int index)
{
    if (index < 0 || index >= rows.size()) return {};
    return storageRowsText({rows[index]});
}
}

HealthPage::HealthPage(QWidget *parent) : QWidget(parent)
{
    hide();
    auto *healthLayout = new QVBoxLayout(this);
    healthNotice_ = plainLabel(tr("ℹ️ Cached drive health"), this);
    healthLayout->addWidget(healthNotice_);
    auto *healthLabel = new QLabel(tr("&Drive:"), this);
    healthDrive_ = new QComboBox(this);
    healthDrive_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    healthDrive_->setMinimumContentsLength(24);
    healthLabel->setBuddy(healthDrive_);
    healthLayout->addWidget(healthLabel);
    healthLayout->addWidget(healthDrive_);
    auto *healthScroll = new QScrollArea(this);
    healthScroll->setWidgetResizable(true);
    healthScroll->setFrameShape(QFrame::NoFrame);
    auto *healthReport = new QWidget(healthScroll);
    healthGroups_ = new QVBoxLayout(healthReport);
    healthScroll->setWidget(healthReport);
    healthLayout->addWidget(healthScroll, 1);
    auto *healthButtons = new QHBoxLayout;
    auto *copyHealth = new QPushButton(tr("Copy selected drive health"), this);
    auto *copyAllHealth = new QPushButton(tr("Copy all drive health"), this);
    healthButtons->addWidget(copyHealth);
    healthButtons->addWidget(copyAllHealth);
    healthButtons->addStretch();
    healthLayout->addLayout(healthButtons);
    connect(healthDrive_, &QComboBox::currentIndexChanged, this, &HealthPage::showHealth);
    connect(copyHealth, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(selectedHealthText(rows_, healthDrive_->currentIndex()));
    });
    connect(copyAllHealth, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(storageRowsText(rows_));
    });
}

void HealthPage::setSnapshot(const QVector<StorageDisplayRow> &rows,
                             const QString &preferredDrive, const QString &notes)
{
    const QString previousDrive = healthDrive_->currentData().toString();
    rows_ = rows;
    healthNotice_->setToolTip(propertyToolTip(tr("Cached UDisks2 health only, separate from device operational status. "
        "Reload does not request SMART updates or self-tests. Missing evidence is unavailable; "
        "no reported warning does not guarantee health. Related drives are shown separately.")
        + "\n\n" + notes));
    {
        const QSignalBlocker blocker(healthDrive_);
        healthDrive_->clear();
        for (const StorageDisplayRow &entry : rows_) {
            healthDrive_->addItem(entry.name, entry.id);
            healthDrive_->setItemData(healthDrive_->count() - 1, entry.source.toHtmlEscaped(), Qt::ToolTipRole);
        }
        int selected = healthDrive_->findData(previousDrive);
        if (selected < 0 && !preferredDrive.isEmpty())
            selected = healthDrive_->findData(preferredDrive);
        healthDrive_->setCurrentIndex(selected < 0 ? 0 : selected);
    }
    showHealth();
}
void HealthPage::showHealth()
{
    while (QLayoutItem *item = healthGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const int row = healthDrive_->currentIndex();
    if (row < 0 || row >= rows_.size()) {
        healthGroups_->addWidget(plainLabel(tr("Cached health unavailable."), healthGroups_->parentWidget()));
        healthGroups_->addStretch();
        return;
    }
    const StorageDisplayRow &entry = rows_[row];
    QVector<QGridLayout *> forms;
    QVector<QLabel *> captions;
    const auto group = [&](const QString &title, const QVector<StorageDisplayField> &fields) {
        auto *box = new QGroupBox(title, healthGroups_->parentWidget());
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        forms.append(form);
        healthGroups_->addWidget(box);
        int fieldRow = 0;
        for (const StorageDisplayField &field : fields) {
            auto *caption = new QLabel(field.label, box);
            caption->setTextFormat(Qt::PlainText);
            captions.append(caption);
            auto *value = plainLabel(field.value, box);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            value->setSizePolicy(policy);
            value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
            value->setMinimumWidth(120);
            const QString hint = propertyToolTip(field.tooltip.isEmpty() ? field.source : field.tooltip + '\n' + field.source);
            caption->setToolTip(hint);
            value->setToolTip(hint);
            form->addWidget(caption, fieldRow, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, fieldRow++, 1);
        }
    };
    if (!entry.fields.isEmpty()) group(tr("Health"), entry.fields);
    for (const StorageDisplayGroup &section : entry.healthGroups) group(section.title, section.fields);
    int captionWidth = 0;
    for (const QLabel *caption : captions) captionWidth = qMax(captionWidth, caption->sizeHint().width());
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    if (!entry.ataAttributes.isEmpty()) {
        auto *box = new QGroupBox(tr("ATA SMART attributes"), healthGroups_->parentWidget());
        auto *layout = new QVBoxLayout(box);
        auto *table = new ReportTable(static_cast<int>(entry.ataAttributes.size()), 7, box);
        table->setHorizontalHeaderLabels({tr("ID"), tr("Name"), tr("Current"), tr("Worst"), tr("Threshold"), tr("Interpreted value"), tr("Flags")});
        const QStringList hints {
            tr("Attribute ID; the drive-specific name is the authoritative identifier."),
            tr("UDisks2's drive-specific attribute identifier."),
            tr("Current normalized value; not necessarily a percentage."),
            tr("Worst normalized value reported by the drive."),
            tr("Drive-reported normalized threshold."),
            tr("UDisks2 interpreted value or smartctl raw counter, as labeled. See the value tooltip for its source and units."),
            tr("Raw attribute flags: bit 0 indicates pre-failure/old-age; bit 1 indicates online/offline collection.")
        };
        for (int column = 0; column < table->columnCount(); ++column) {
            table->horizontalHeaderItem(column)->setToolTip(hints[column]);
        }
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (int index = 0; index < entry.ataAttributes.size(); ++index) {
            const StorageDisplayAtaAttribute &attribute = entry.ataAttributes[index];
            const QStringList values {attribute.id, attribute.name, attribute.current, attribute.worst,
                                      attribute.threshold, attribute.interpreted, attribute.flags};
            for (int column = 0; column < values.size(); ++column) {
                auto *item = new QTableWidgetItem(values[column]);
                item->setToolTip(propertyToolTip((column == 5 && !attribute.interpretedTooltip.isEmpty()
                    ? attribute.interpretedTooltip + '\n' : QString()) + hints[column] + '\n' + attribute.source));
                if (column == 0 || (column >= 2 && column <= 4) || column == 6)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                table->setItem(index, column, item);
                if (column == 5 && !attribute.counterNotice.isEmpty()) {
                    // Paint the prefix and value together: cell widgets would overlap
                    // the underlying item text and interfere with row sizing.
                    item->setText(QStringLiteral("ℹ️ ") + attribute.interpreted);
                    item->setData(Qt::UserRole, attribute.counterNotice);
                }
            }
        }
        connect(table, &QTableWidget::itemClicked, this, [this](QTableWidgetItem *item) {
            const QString notice = item->data(Qt::UserRole).toString();
            if (!notice.isEmpty()) QMessageBox::information(this, tr("SMART counter information"), notice);
        });
        layout->addWidget(table);
        table->fitContents();
        healthGroups_->addWidget(box);
    }
    healthGroups_->addStretch();
}
