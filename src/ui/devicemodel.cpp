#include "devicemodel.h"
#include "devicelabel.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStringList>
#include <algorithm>
#include <utility>

DeviceModel::DeviceModel(QObject *parent) : QAbstractItemModel(parent) {}
DeviceModel::Node *DeviceModel::node(const QModelIndex &i) const
{
    return i.isValid() ? static_cast<Node *>(i.internalPointer()) : const_cast<Node *>(&root_);
}
QModelIndex DeviceModel::index(int row, int column, const QModelIndex &p) const
{
    if (column != 0 || row < 0 || (p.isValid() && p.column() != 0)) return {};
    Node *n = node(p);
    if (row >= static_cast<int>(n->children.size())) return {};
    return createIndex(row, column, n->children[static_cast<size_t>(row)].get());
}
QModelIndex DeviceModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};
    Node *p = node(child)->parent;
    return !p || p == &root_ ? QModelIndex() : createIndex(p->row, 0, p);
}
int DeviceModel::rowCount(const QModelIndex &p) const
{
    return p.isValid() && p.column() != 0 ? 0 : static_cast<int>(node(p)->children.size());
}
QVariant DeviceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section == 0 && orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return tr("Devices by type");
    return {};
}
QVariant DeviceModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid()) return {};
    const Node *n = node(i);
    const auto found = lookup_.constFind(n->path);
    const Device *d = found == lookup_.cend() ? nullptr : &devices_.at(found.value());
    if (role == CategoryRole) return n->category;
    if (role == PathRole) return d ? QVariant(d->path) : QVariant();
    if (role == GenerationRole) return d ? QVariant::fromValue(d->generation) : QVariant();
    if (role == Qt::DisplayRole) {
        if (!d) return categoryLabel(n->category);
        return n->label;
    }
    if (role == Qt::DecorationRole)
        return QIcon::fromTheme(categoryIcon(n->category), QApplication::style()->standardIcon(
            d ? QStyle::SP_ComputerIcon : QStyle::SP_DirIcon));
    if (role == Qt::ToolTipRole) {
        if (!d) return categoryLabel(n->category).toHtmlEscaped();
        // Escaping prevents device-controlled strings being interpreted as markup.
        QString tooltip = QStringLiteral("%1\n%2\n%3: %4\n%5: %6")
            .arg(n->label, d->subsystem == "efivarfs" && !showInternal_ ? d->parentPath : d->path,
                 tr("Subsystem"), d->subsystem, tr("Raw name source"), d->nameSource);
        tooltip += QStringLiteral("\n%1: %2\n%3: %4")
            .arg(tr("Raw name"), d->name, tr("Kernel name"),
                 d->subsystem == "efivarfs" && !showInternal_ ? d->name : d->sysname);
        if (d->subsystem == "power_supply") {
            for (const char *key : {"type", "manufacturer", "model_name"}) {
                const auto attribute = d->attributes.value(QLatin1String(key));
                if (attribute.state == ReadState::Available && !attribute.value.isEmpty())
                    tooltip += QStringLiteral("\n%1: %2").arg(QLatin1String(key), attribute.value);
            }
        }
        if (!d->representedByPath.isEmpty())
            tooltip += QStringLiteral("\n%1: %2").arg(tr("Represented by"), d->representedByPath);
        QStringList grouped;
        for (const auto &record : devices_)
            if (record.representedByPath == d->path) grouped.append(record.path);
        if (!grouped.isEmpty())
            tooltip += QStringLiteral("\n%1:\n%2").arg(tr("Grouped records"), grouped.join('\n'));
        return QStringLiteral("<qt>%1</qt>").arg(tooltip.toHtmlEscaped().replace('\n', "<br>"));
    }
    return {};
}
void DeviceModel::setInventory(QVector<Device> devices)
{
    reconcile(devices, devices_, generation_);
    beginResetModel();
    devices_ = std::move(devices);
    rebuild();
    endResetModel();
}
void DeviceModel::setShowInternal(bool show)
{
    if (show == showInternal_) return;
    beginResetModel();
    showInternal_ = show;
    rebuild();
    endResetModel();
}
void DeviceModel::rebuild()
{
    root_.children.clear();
    lookup_.clear();
    visible_ = 0;
    for (int i = 0; i < devices_.size(); ++i) lookup_.insert(devices_.at(i).path, i);
    for (const auto &category : categories()) {
        QVector<const Device *> members;
        for (const auto &d : devices_)
            if (d.category == QLatin1String(category.id) && (showInternal_ || !d.hidden)) members.append(&d);
        if (members.isEmpty()) continue;
        QHash<QString, QString> labels;
        QHash<QString, int> counts;
        for (const Device *d : members) {
            const QString label = deviceDisplayName(*d, showInternal_);
            labels.insert(d->path, label);
            ++counts[label];
        }
        QHash<QString, int> namespaceNumbers;
        for (const Device *d : members) {
            const QString label = labels.value(d->path);
            if (counts.value(label) <= 1) continue;
            if (d->subsystem == "efivarfs" && !showInternal_)
                labels[d->path] = tr("%1 (namespace %2)").arg(label).arg(++namespaceNumbers[label]);
            else labels[d->path] = deviceDisplayName(*d, true);
        }
        std::sort(members.begin(), members.end(), [&labels](const Device *a, const Device *b) {
            const int cmp = QString::compare(labels.value(a->path), labels.value(b->path), Qt::CaseInsensitive);
            return cmp == 0 ? a->path < b->path : cmp < 0;
        });
        auto group = std::make_unique<Node>();
        group->parent = &root_;
        group->row = static_cast<int>(root_.children.size());
        group->category = QLatin1String(category.id);
        for (const Device *d : members) {
            auto child = std::make_unique<Node>();
            child->parent = group.get();
            child->row = static_cast<int>(group->children.size());
            child->category = d->category;
            child->path = d->path;
            child->label = labels.value(d->path);
            group->children.push_back(std::move(child));
            ++visible_;
        }
        root_.children.push_back(std::move(group));
    }
}
QModelIndex DeviceModel::findDevice(const QString &path, quint64 generation) const
{
    const auto it = lookup_.constFind(path);
    if (it == lookup_.cend() || devices_.at(it.value()).generation != generation) return {};
    for (const auto &group : root_.children)
        for (const auto &child : group->children)
            if (child->path == path) return createIndex(child->row, 0, child.get());
    return {};
}
