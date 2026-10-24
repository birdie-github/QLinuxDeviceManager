#include "reporttable.h"
#include <QHeaderView>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTimer>

namespace {
class ReportHeader final : public QHeaderView {
public:
    explicit ReportHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {}
    int wrappedHeight() const
    {
        int height = 0;
        for (int index = 0; index < count(); ++index) {
            if (isSectionHidden(index)) continue;
            QStyleOptionHeaderV2 option;
            initStyleOption(&option);
            initStyleOptionForIndex(&option, index);
            const int baseHeight = QHeaderView::sectionSizeFromContents(index).height();
            option.rect = QRect(0, 0, sectionSize(index), baseHeight);
            const QRect label = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
            const QRect wrapped = fontMetrics().boundingRect(QRect(0, 0, qMax(1, label.width()), QWIDGETSIZE_MAX),
                Qt::AlignCenter | Qt::TextWordWrap, option.text);
            height = qMax(height, qMax(baseHeight, wrapped.height() + baseHeight - label.height()));
        }
        return height;
    }
protected:
    void paintSection(QPainter *painter, const QRect &rect, int index) const override
    {
        if (!rect.isValid()) return;
        QStyleOptionHeaderV2 option;
        initStyleOption(&option);
        initStyleOptionForIndex(&option, index);
        option.rect = rect;
        const QString text = option.text;
        const QRect label = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        option.text.clear();
        painter->save();
        style()->drawControl(QStyle::CE_Header, &option, painter, this);
        painter->setFont(font());
        style()->drawItemText(painter, label, Qt::AlignCenter | Qt::TextWordWrap,
            option.palette, option.state.testFlag(QStyle::State_Enabled), text, QPalette::ButtonText);
        painter->restore();
    }
};
}

ReportTable::ReportTable(QWidget *parent) : ReportTable(0, 0, parent) {}
ReportTable::ReportTable(int rows, int columns, QWidget *parent) : QTableWidget(rows, columns, parent)
{
    setHorizontalHeader(new ReportHeader(this));
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setStretchLastSection(false);
    verticalHeader()->hide();
    setAlternatingRowColors(true);
    setShowGrid(true);
    setGridStyle(Qt::SolidLine);
    setWordWrap(true);
    setTextElideMode(Qt::ElideNone);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int column, int, int width) {
        if (!fitting_ && preferredWidths_.size() == columnCount() && column >= 0 && column < columnCount()) {
            qint64 total = 0;
            for (int preferred : preferredWidths_) total += preferred;
            const int available = viewport()->width();
            const qint64 others = total - preferredWidths_[column];
            // Convert a dragged, expanded width back to its preferred width.
            // This keeps the requested section size while spare space is shared.
            const qint64 preferred = total < available && others > 0 && width <= available - others
                ? (static_cast<qint64>(width) * others + (available - width) / 2) / (available - width)
                : width;
            preferredWidths_[column] = static_cast<int>(qBound<qint64>(
                static_cast<qint64>(horizontalHeader()->minimumSectionSize()), preferred,
                static_cast<qint64>(QWIDGETSIZE_MAX)));
        }
        fitContents();
    });
    connect(model(), &QAbstractItemModel::dataChanged, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::headerDataChanged, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::rowsInserted, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::rowsRemoved, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::columnsInserted, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::columnsRemoved, this, [this] { fitContents(); });
    connect(model(), &QAbstractItemModel::modelReset, this, [this] { fitContents(); });
    fitContents();
}

void ReportTable::fitContents()
{
    if (fitQueued_ || fitting_) return;
    fitQueued_ = true;
    // Coalesce row/item population and wait for the surrounding layout to settle.
    QTimer::singleShot(0, this, [this] {
        fitQueued_ = false;
        fitting_ = true;
        if (initializedColumns_ != columnCount()) {
            // Keep preferred widths separate from the expanded viewport widths.
            // Otherwise an enlarged window becomes the table's new minimum size.
            const int limit = qMax(160, 30 * fontMetrics().averageCharWidth());
            preferredWidths_.resize(columnCount());
            for (int column = 0; column < columnCount(); ++column)
                preferredWidths_[column] = qBound(horizontalHeader()->minimumSectionSize(),
                    qMax(sizeHintForColumn(column) + 2 * frameWidth(), horizontalHeader()->sectionSizeHint(column)), limit);
            initializedColumns_ = rowCount() ? columnCount() : -1;
        }
        qint64 preferredTotal = 0;
        if (preferredWidths_.size() == columnCount()) {
            for (int width : preferredWidths_) preferredTotal += width;
            const qint64 available = qMax<qint64>(preferredTotal, static_cast<qint64>(viewport()->width()));
            qint64 preferredBefore = 0, displayedBefore = 0;
            for (int column = 0; column < columnCount() && preferredTotal > 0; ++column) {
                preferredBefore += preferredWidths_[column];
                const qint64 displayedEnd = available * preferredBefore / preferredTotal;
                setColumnWidth(column, static_cast<int>(displayedEnd - displayedBefore));
                displayedBefore = displayedEnd;
            }
        }
        auto *header = static_cast<ReportHeader *>(horizontalHeader());
        const int headerHeight = header->isHidden() ? 0 : header->wrappedHeight();
        if (!header->isHidden() && header->height() != headerHeight) header->setFixedHeight(headerHeight);
        resizeRowsToContents();
        const int height = qMin(QWIDGETSIZE_MAX, verticalHeader()->length() + headerHeight + 2 * frameWidth());
        if (minimumHeight() != height || maximumHeight() != height) setFixedHeight(height);
        const int width = static_cast<int>(qMin<qint64>(static_cast<qint64>(QWIDGETSIZE_MAX),
            preferredTotal + 2 * frameWidth()));
        if (minimumWidth() != width) setMinimumWidth(width);
        fitting_ = false;
    });
}

void ReportTable::resizeEvent(QResizeEvent *event)
{
    QTableWidget::resizeEvent(event);
    fitContents();
}
void ReportTable::changeEvent(QEvent *event)
{
    QTableWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::LayoutDirectionChange) fitContents();
}
void ReportTable::scrollTo(const QModelIndex &index, ScrollHint hint)
{
    QTableWidget::scrollTo(index, hint);
    // Keep keyboard navigation usable now that the outer page owns scrolling.
    if (!hasFocus() || !index.isValid()) return;
    for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget()) {
        auto *scroll = qobject_cast<QScrollArea *>(parent);
        if (!scroll || !scroll->widget()) continue;
        const QPoint position = viewport()->mapTo(scroll->widget(), visualRect(index).center());
        scroll->ensureVisible(position.x(), position.y());
        break;
    }
}
