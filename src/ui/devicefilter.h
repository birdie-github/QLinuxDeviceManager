#pragma once
#include "devicesearch.h"
#include <QSortFilterProxyModel>

class DeviceFilter final : public QSortFilterProxyModel {
public:
    explicit DeviceFilter(QObject *parent = nullptr);
    void setQuery(const QString &query, bool deep);
    void addMatches(const QVector<SearchMatch> &matches);
    bool active() const { return !query_.isEmpty(); }
    int visibleCount() const;
    QString matchReason(const QModelIndex &index) const;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    QString query_;
    bool deep_ = false;
    QHash<QString, SearchMatch> matches_;
};
