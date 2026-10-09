#include "devicemodel.h"
#include "devicelabel.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStringList>
#include <algorithm>
#include <functional>
#include <QSet>
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
        return viewLabel(view_);
    return {};
}
QVariant DeviceModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid()) return {};
    const Node *n = node(i);
    const auto found = lookup_.constFind(n->path);
    const Device *d = found == lookup_.cend() ? nullptr : &devices_.at(found.value());
    if (role == NodeKeyRole) return n->key;
    if (role == CategoryRole) return n->category;
    if (role == PathRole) return d ? QVariant(d->path) : QVariant();
    if (role == GenerationRole) return d ? QVariant::fromValue(d->generation) : QVariant();
    if (role == Qt::DisplayRole) return n->label;
    if (role == Qt::DecorationRole)
        return QIcon::fromTheme(categoryIcon(n->category), QApplication::style()->standardIcon(
            d ? QStyle::SP_ComputerIcon : QStyle::SP_DirIcon));
    if (role == Qt::ToolTipRole) {
        if (!d) return n->label.toHtmlEscaped();
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
        if (n->context) tooltip += QStringLiteral("\n") + tr("Ancestor retained for connection context");
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
    QVector<const Device *> members;
    for (const Device &d : devices_) members.append(&d);
    std::sort(members.begin(), members.end(), [](const Device *a, const Device *b) {
        return a->path < b->path;
    });
    QHash<QString, QString> labels;
    QHash<QString, int> counts;
    for (const Device *d : members) {
        const QString label = deviceDisplayName(*d, showInternal_);
        labels.insert(d->path, label);
        if (showInternal_ || !d->hidden) ++counts[d->category + ":" + label];
    }
    QHash<QString, int> namespaceNumbers;
    for (const Device *d : members) {
        const QString label = labels.value(d->path);
        if (counts.value(d->category + ":" + label) <= 1) continue;
        if (d->subsystem == "efivarfs" && !showInternal_)
            labels[d->path] = tr("%1 (namespace %2)").arg(label).arg(++namespaceNumbers[label]);
        else labels[d->path] = deviceDisplayName(*d, true);
    }
    std::sort(members.begin(), members.end(), [&labels](const Device *a, const Device *b) {
        const int cmp = QString::compare(labels.value(a->path), labels.value(b->path), Qt::CaseInsensitive);
        return cmp == 0 ? a->path < b->path : cmp < 0;
    });
    const auto add = [](Node *parent, const QString &key, const QString &label,
                        const QString &category, const QString &path = QString()) {
        auto item = std::make_unique<Node>();
        item->parent = parent;
        item->row = static_cast<int>(parent->children.size());
        item->key = key;
        item->label = label;
        item->category = category;
        item->path = path;
        Node *result = item.get();
        parent->children.push_back(std::move(item));
        return result;
    };
    const auto deviceNode = [&](Node *parent, const Device &d) {
        return add(parent, "device:" + d.path, labels.value(d.path), d.category, d.path);
    };
    const auto driverKey = [](const Device &d) {
        return d.driver.isEmpty() ? QStringLiteral("unbound")
            : d.subsystem + ':' + d.driver;
    };
    const auto driverLabel = [](const Device &d) {
        return d.driver.isEmpty() ? tr("No directly bound driver")
            : tr("%1: %2 (driver)").arg(d.subsystem, d.driver);
    };
    QHash<QString, Node *> groups;
    const auto group = [&](Node *parent, const QString &key, const QString &label,
                           const QString &category) {
        if (groups.contains(key)) return groups.value(key);
        Node *result = add(parent, key, label, category);
        groups.insert(key, result);
        return result;
    };
    if (view_ == View::Connection) {
        // Only real inventory parent links; hidden ancestors provide context.
        QSet<QString> included;
        for (const Device *d : members) {
            if (!showInternal_ && d->hidden) continue;
            QString path = d->path;
            QSet<QString> chain;
            while (lookup_.contains(path) && !chain.contains(path)) {
                chain.insert(path);
                included.insert(path);
                path = devices_.at(lookup_.value(path)).parentPath;
            }
        }
        QHash<QString, Node *> nodes;
        QSet<QString> building;
        std::function<Node *(const Device &)> ensure = [&](const Device &d) -> Node * {
            if (nodes.contains(d.path)) return nodes.value(d.path);
            building.insert(d.path);
            Node *parent = &root_;
            if (included.contains(d.parentPath) && !building.contains(d.parentPath))
                parent = ensure(devices_.at(lookup_.value(d.parentPath)));
            Node *item = deviceNode(parent, d);
            item->context = !showInternal_ && d.hidden;
            nodes.insert(d.path, item);
            building.remove(d.path);
            return item;
        };
        for (const Device *d : members) if (included.contains(d->path)) ensure(*d);
    } else {
        for (const Device *d : members) {
            if (!showInternal_ && d->hidden) continue;
            Node *parent = &root_;
            if (view_ == View::Type || view_ == View::DriversByType)
                parent = group(parent, "category:" + d->category, categoryLabel(d->category), d->category);
            if (view_ == View::DevicesByDriver || view_ == View::DriversByType) {
                const QString prefix = view_ == View::DriversByType ? d->category + ':' : QString();
                parent = group(parent, "driver:" + prefix + driverKey(*d), driverLabel(*d), d->category);
            }
            Node *item = deviceNode(parent, *d);
            if (view_ != View::DriversByDevice) continue;
            Node *driver = add(item, item->key + "/driver", driverLabel(*d), d->category);
            if (!d->driver.isEmpty())
                add(driver, item->key + "/module", d->driverModule.isEmpty()
                    ? tr("Owning module / built-in status undetermined")
                    : tr("%1 (kernel module)").arg(d->driverModule), d->category);
            // Explicitly separate ancestor binding from the selected device's binding.
            QString path = d->parentPath;
            QSet<QString> visited{d->path};
            while (lookup_.contains(path) && !visited.contains(path)) {
                visited.insert(path);
                const Device &ancestor = devices_.at(lookup_.value(path));
                if (!ancestor.driver.isEmpty()) {
                    Node *relation = add(item, item->key + "/parent-driver",
                        tr("Parent-device driver: %1 — %2").arg(driverLabel(ancestor), labels.value(path)),
                        ancestor.category);
                    if (!ancestor.driverModule.isEmpty())
                        add(relation, item->key + "/parent-module",
                            tr("%1 (kernel module)").arg(ancestor.driverModule), ancestor.category);
                    break;
                }
                path = ancestor.parentPath;
            }
        }
    }
    // Stable sibling order, including connection ancestors inserted before children.
    std::function<void(Node *)> sort = [&](Node *parent) {
        std::sort(parent->children.begin(), parent->children.end(), [](const auto &a, const auto &b) {
            const int cmp = QString::compare(a->label, b->label, Qt::CaseInsensitive);
            return cmp == 0 ? a->key < b->key : cmp < 0;
        });
        for (size_t row = 0; row < parent->children.size(); ++row) {
            Node *child = parent->children[row].get();
            child->row = static_cast<int>(row);
            if (!child->path.isEmpty()) ++visible_;
            sort(child);
        }
    };
    sort(&root_);
}
QModelIndex DeviceModel::findDevice(const QString &path, quint64 generation) const
{
    const auto it = lookup_.constFind(path);
    if (it == lookup_.cend() || devices_.at(it.value()).generation != generation) return {};
    std::function<QModelIndex(const Node *)> find = [&](const Node *parent) -> QModelIndex {
        for (const auto &child : parent->children) {
            if (child->path == path) return createIndex(child->row, 0, child.get());
            const QModelIndex found = find(child.get());
            if (found.isValid()) return found;
        }
        return {};
    };
    return find(&root_);
}

std::optional<Device> DeviceModel::device(const QString &path, quint64 generation) const
{
    const auto it = lookup_.constFind(path);
    if (it == lookup_.cend() || devices_.at(it.value()).generation != generation) return std::nullopt;
    return devices_.at(it.value()); // Owned copy; callers never retain inventory pointers.
}

QVector<SearchRecord> DeviceModel::searchRecords() const
{
    QVector<SearchRecord> records;
    records.reserve(visible_);
    QHash<QString, QStringList> grouped;
    for (const Device &d : devices_)
        if (!d.representedByPath.isEmpty()) grouped[d.representedByPath].append(d.path);
    std::function<void(const Node *)> collect = [&](const Node *parent) {
        for (const auto &child : parent->children) {
            if (!child->path.isEmpty()) {
                const Device &d = devices_.at(lookup_.value(child->path));
                records.append({d, child->label, grouped.value(d.path).join('\n')});
            }
            collect(child.get());
        }
    };
    collect(&root_);
    return records;
}

QString DeviceModel::viewId(View view)
{
    switch (view) {
    case View::Type: return "type";
    case View::Connection: return "connection";
    case View::DevicesByDriver: return "devicesByDriver";
    case View::DriversByDevice: return "driversByDevice";
    case View::DriversByType: return "driversByType";
    }
    return "type";
}
QString DeviceModel::viewLabel(View view)
{
    switch (view) {
    case View::Type: return tr("Devices by type");
    case View::Connection: return tr("Devices by connection");
    case View::DevicesByDriver: return tr("Devices by driver");
    case View::DriversByDevice: return tr("Drivers by device");
    case View::DriversByType: return tr("Drivers by type");
    }
    return {};
}
void DeviceModel::setView(View view)
{
    if (view_ == view) return;
    beginResetModel();
    view_ = view;
    rebuild();
    endResetModel();
}
