#include "devicefilter.h"
#include "devicemodel.h"
#include <functional>

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
    if (path.isEmpty()) {
        // Keep driver/module relationship children when their device matches.
        for (QModelIndex owner = parent; owner.isValid(); owner = owner.parent()) {
            const QString ownerPath = owner.data(DeviceModel::PathRole).toString();
            if (ownerPath.isEmpty()) continue;
            if (owner.data(Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive)) return true;
            const auto match = matches_.constFind(ownerPath);
            return deep_ && match != matches_.cend()
                && match->generation == owner.data(DeviceModel::GenerationRole).toULongLong();
        }
        return false; // Recursive filtering retains matching ancestor groups.
    }
    if (index.data(Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive)) return true;
    const auto match = matches_.constFind(path);
    return deep_ && match != matches_.cend()
        && match->generation == index.data(DeviceModel::GenerationRole).toULongLong();
}
int DeviceFilter::visibleCount() const
{
    int count = 0;
    std::function<void(const QModelIndex &)> visit = [&](const QModelIndex &parent) {
        for (int row = 0; row < rowCount(parent); ++row) {
            const QModelIndex child = index(row, 0, parent);
            if (!child.data(DeviceModel::PathRole).toString().isEmpty()) ++count;
            visit(child);
        }
    };
    visit({});
    return count;
}
QString DeviceFilter::matchReason(const QModelIndex &index) const
{
    if (!active() || !deep_ || index.data(Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive)) return {};
    const auto match = matches_.constFind(index.data(DeviceModel::PathRole).toString());
    return match != matches_.cend() && match->generation == index.data(DeviceModel::GenerationRole).toULongLong()
        ? match->field : QString();
}
