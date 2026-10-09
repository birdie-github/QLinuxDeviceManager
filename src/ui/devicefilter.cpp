#include "devicefilter.h"
#include "devicemodel.h"

DeviceFilter::DeviceFilter(QObject *parent) : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
}
void DeviceFilter::setQuery(const QString &query, bool deep)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    query_ = query;
    deep_ = deep;
    matches_.clear();
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}
void DeviceFilter::addMatches(const QVector<SearchMatch> &matches)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    for (const SearchMatch &match : matches) matches_.insert(match.path, match);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}
bool DeviceFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!active()) return true;
    const QModelIndex index = sourceModel()->index(row, 0, parent);
    const QString path = index.data(DeviceModel::PathRole).toString();
    if (path.isEmpty()) return false; // Recursive filtering retains matching groups.
    if (index.data(Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive)) return true;
    const auto match = matches_.constFind(path);
    return deep_ && match != matches_.cend()
        && match->generation == index.data(DeviceModel::GenerationRole).toULongLong();
}
int DeviceFilter::visibleCount() const
{
    int count = 0;
    for (int row = 0; row < rowCount(); ++row) count += rowCount(index(row, 0));
    return count;
}
QString DeviceFilter::matchReason(const QModelIndex &index) const
{
    if (!active() || !deep_ || index.data(Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive)) return {};
    const auto match = matches_.constFind(index.data(DeviceModel::PathRole).toString());
    return match != matches_.cend() && match->generation == index.data(DeviceModel::GenerationRole).toULongLong()
        ? match->field : QString();
}
