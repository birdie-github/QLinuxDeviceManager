#include "devicemodel.h"
#include "devicelabel.h"
#include "resourceformat.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStringList>
#include <algorithm>
#include <functional>
#include <QSet>
#include <QSysInfo>
#include <utility>

namespace {
bool sameMetadata(const Device &a, const Device &b)
{
    if (a.path != b.path || a.parentPath != b.parentPath || a.subsystem != b.subsystem
        || a.sysname != b.sysname || a.devtype != b.devtype || a.driver != b.driver
        || a.driverModule != b.driverModule || a.properties != b.properties
        || a.name != b.name || a.nameSource != b.nameSource || a.nameTranslated != b.nameTranslated
        || a.nameCandidate != b.nameCandidate || a.representedByPath != b.representedByPath
        || a.propertySources != b.propertySources || a.category != b.category
        || a.incarnation != b.incarnation || a.hidden != b.hidden || a.attributes.size() != b.attributes.size()
        || !sameDeviceResources(a.resources, b.resources))
        return false;
    for (auto it = a.attributes.cbegin(); it != a.attributes.cend(); ++it) {
        const auto other = b.attributes.constFind(it.key());
        if (other == b.attributes.cend() || it->state != other->state
            || it->value != other->value || it->error != other->error) return false;
    }
    return true;
}
}
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
    const auto found = lookup_.constFind(n->path.isEmpty() ? n->ownerPath : n->path);
    const Device *d = found == lookup_.cend() ? nullptr : &devices_.at(found.value());
    if (d && d->generation != n->generation) d = nullptr;
    if (role == NodeKeyRole) return n->key;
    if (role == CategoryRole) return n->category;
    if (role == ResourceRole) return !n->ownerPath.isEmpty();
    if (role == OwnerPathRole) return d && !n->ownerPath.isEmpty() ? QVariant(d->path) : QVariant();
    if (role == OwnerGenerationRole) return d && !n->ownerPath.isEmpty() ? QVariant::fromValue(d->generation) : QVariant();
    if (role == PathRole) return d && !n->path.isEmpty() ? QVariant(d->path) : QVariant();
    if (role == GenerationRole) return d && !n->path.isEmpty() ? QVariant::fromValue(d->generation) : QVariant();
    if (role == Qt::DisplayRole) return n->label;
    if (role == Qt::DecorationRole && n->key == "computer")
        return QIcon::fromTheme("computer", QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    if (role == Qt::DecorationRole)
        return QIcon::fromTheme(categoryIcon(n->category), QApplication::style()->standardIcon(
            d ? QStyle::SP_ComputerIcon : QStyle::SP_DirIcon));
    if (role == Qt::ToolTipRole) {
        if (!n->description.isEmpty())
            return QStringLiteral("<qt>%1</qt>").arg(n->description.toHtmlEscaped().replace('\n', "<br>"));
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
bool DeviceModel::setInventory(QVector<Device> devices, const QSet<QString> &removedPaths,
                               bool identityLost)
{
    QVector<Device> previous;
    if (!identityLost) {
        for (const Device &d : devices_) {
            bool removed = false;
            QString path = d.path;
            while (!path.isEmpty()) {
                if (removedPaths.contains(path)) { removed = true; break; }
                const int slash = path.lastIndexOf('/');
                if (slash < 0) break;
                path.truncate(slash);
            }
            if (!removed) previous.append(d);
        }
    }
    reconcile(devices, previous, generation_);
    QSet<QString> changedPaths;
    for (const Device &d : devices) {
        const auto old = lookup_.constFind(d.path);
        if (old == lookup_.cend() || d.generation != devices_.at(old.value()).generation
            || !sameMetadata(d, devices_.at(old.value()))) changedPaths.insert(d.path);
    }
    // Grouped-record tooltips also change when a member disappears or moves.
    QHash<QString, QString> representatives;
    for (const Device &d : devices) representatives.insert(d.path, d.representedByPath);
    for (const Device &d : devices_) {
        if (!representatives.contains(d.path) || representatives.value(d.path) != d.representedByPath) {
            changedPaths.insert(d.path);
            if (!d.representedByPath.isEmpty()) changedPaths.insert(d.representedByPath);
        }
    }
    for (const Device &d : devices)
        if (changedPaths.contains(d.path) && !d.representedByPath.isEmpty()) changedPaths.insert(d.representedByPath);
    const QString hostname = QSysInfo::machineHostName();
    const QString computerLabel = hostname.isEmpty() ? tr("This computer") : hostname;
    if (changedPaths.isEmpty() && !root_.children.empty() && root_.children.front()->label == computerLabel)
        return false;
    // Build a temporary projection, then update only changed branches. Existing
    // Node addresses (and persistent indexes) survive unchanged inventory entries.
    auto existing = std::move(root_.children);
    devices_ = std::move(devices);
    rebuild();
    Node desired;
    desired.children = std::move(root_.children);
    root_.children = std::move(existing);
    synchronize(&root_, &desired, changedPaths);
    return true;
}
void DeviceModel::synchronize(Node *parentNode, Node *desired, const QSet<QString> &changedPaths)
{
    const QModelIndex parentIndex = parentNode == &root_ ? QModelIndex()
        : createIndex(parentNode->row, 0, parentNode);
    const auto same = [](const Node &a, const Node &b) {
        return a.key == b.key && a.generation == b.generation;
    };
    const auto rows = [](Node *parent) {
        for (size_t i = 0; i < parent->children.size(); ++i) {
            parent->children[i]->parent = parent;
            parent->children[i]->row = static_cast<int>(i);
        }
    };
    for (int i = static_cast<int>(parentNode->children.size()) - 1; i >= 0; --i) {
        const Node &old = *parentNode->children[static_cast<size_t>(i)];
        const bool retained = std::any_of(desired->children.begin(), desired->children.end(),
            [&](const auto &next) { return same(old, *next); });
        if (retained) continue;
        beginRemoveRows(parentIndex, i, i);
        parentNode->children.erase(parentNode->children.begin() + i);
        rows(parentNode);
        endRemoveRows();
    }
    for (size_t i = 0; i < desired->children.size(); ++i) {
        auto &next = desired->children[i];
        size_t found = i;
        while (found < parentNode->children.size() && !same(*parentNode->children[found], *next)) ++found;
        if (found == parentNode->children.size()) {
            beginInsertRows(parentIndex, static_cast<int>(i), static_cast<int>(i));
            parentNode->children.insert(parentNode->children.begin() + static_cast<std::ptrdiff_t>(i), std::move(next));
            // Children of the inserted subtree already point to their owned parents.
            rows(parentNode);
            endInsertRows();
            continue;
        }
        if (found != i) {
            beginMoveRows(parentIndex, static_cast<int>(found), static_cast<int>(found), parentIndex, static_cast<int>(i));
            auto moving = std::move(parentNode->children[found]);
            parentNode->children.erase(parentNode->children.begin() + static_cast<std::ptrdiff_t>(found));
            parentNode->children.insert(parentNode->children.begin() + static_cast<std::ptrdiff_t>(i), std::move(moving));
            rows(parentNode);
            endMoveRows();
        }
        Node *current = parentNode->children[i].get();
        const bool changed = current->label != next->label || current->category != next->category
            || current->context != next->context || current->description != next->description
            || changedPaths.contains(current->path);
        current->label = next->label;
        current->category = next->category;
        current->context = next->context;
        current->description = next->description;
        current->ownerPath = next->ownerPath;
        // Metadata, grouped tooltips and filter matches may change without a label change.
        if (changed) {
            const QModelIndex index = createIndex(current->row, 0, current);
            emit dataChanged(index, index);
        }
        synchronize(current, next.get(), changedPaths);
    }
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
    const QString hostname = QSysInfo::machineHostName();
    Node *computer = add(&root_, "computer", hostname.isEmpty() ? tr("This computer") : hostname, {});
    const auto deviceNode = [&](Node *parent, const Device &d) {
        const QString suffix = view_ == View::ResourcesByType ? ':' + parent->key : QString();
        Node *item = add(parent, "device:" + d.path + suffix, labels.value(d.path), d.category, d.path);
        item->generation = d.generation;
        return item;
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
    const bool resourcesByType = view_ == View::ResourcesByType;
    const bool resourcesByConnection = view_ == View::ResourcesByConnection;
    const auto hasResources = [](const Device &d) { return d.resources && d.resources->hasInformation(); };
    const auto resourceNode = [&](Node *parent, const Device &d, const ResourceRow &row) {
        QString label = tr("%1: %2").arg(row.type, row.setting);
        if (!row.details.isEmpty()) label += QStringLiteral(" — ") + row.details;
        Node *item = add(parent, parent->key + "/resource:" + row.key, label, d.category);
        item->ownerPath = d.path;
        item->generation = d.generation;
        item->description = label + '\n' + tr("Device: %1").arg(labels.value(d.path))
            + '\n' + tr("Source: %1").arg(row.source);
    };
    if (resourcesByType) {
        for (const Device *d : members) {
            if ((!showInternal_ && d->hidden) || !hasResources(*d)) continue;
            QHash<QString, Node *> deviceGroups;
            for (const ResourceRow &row : resourceRows(*d->resources)) {
                const QString &type = row.group;
                Node *category = group(computer, "resource-type:" + type, row.type, "other");
                Node *item = deviceGroups.value(type, nullptr);
                if (!item) { item = deviceNode(category, *d); deviceGroups.insert(type, item); }
                resourceNode(item, *d, row);
            }
        }
    } else if (view_ == View::Connection || resourcesByConnection) {
        // Only real inventory parent links; hidden ancestors provide context.
        QSet<QString> included;
        for (const Device *d : members) {
            if ((!showInternal_ && d->hidden) || (resourcesByConnection && !hasResources(*d))) continue;
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
            Node *parent = computer;
            if (included.contains(d.parentPath) && !building.contains(d.parentPath))
                parent = ensure(devices_.at(lookup_.value(d.parentPath)));
            Node *item = deviceNode(parent, d);
            item->context = !showInternal_ && d.hidden;
            nodes.insert(d.path, item);
            building.remove(d.path);
            return item;
        };
        for (const Device *d : members) if (included.contains(d->path)) {
            Node *item = ensure(*d);
            if (resourcesByConnection && (showInternal_ || !d->hidden) && hasResources(*d))
                for (const ResourceRow &row : resourceRows(*d->resources)) resourceNode(item, *d, row);
        }
    } else {
        for (const Device *d : members) {
            if (!showInternal_ && d->hidden) continue;
            Node *parent = computer;
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
    QSet<QString> visiblePaths;
    std::function<void(Node *)> sort = [&](Node *parent) {
        std::sort(parent->children.begin(), parent->children.end(), [](const auto &a, const auto &b) {
            const int cmp = QString::compare(a->label, b->label, Qt::CaseInsensitive);
            return cmp == 0 ? a->key < b->key : cmp < 0;
        });
        for (size_t row = 0; row < parent->children.size(); ++row) {
            Node *child = parent->children[row].get();
            child->row = static_cast<int>(row);
            if (!child->path.isEmpty()) visiblePaths.insert(child->path);
            sort(child);
        }
    };
    sort(&root_);
    visible_ = static_cast<int>(visiblePaths.size());
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

QModelIndex DeviceModel::findNode(const QString &key, const QString &path, quint64 generation) const
{
    const auto record = lookup_.constFind(path);
    if (record == lookup_.cend() || devices_.at(record.value()).generation != generation) return {};
    std::function<QModelIndex(const Node *)> find = [&](const Node *parent) -> QModelIndex {
        for (const auto &child : parent->children) {
            if (child->key == key && child->generation == generation
                && (child->path == path || child->ownerPath == path))
                return createIndex(child->row, 0, child.get());
            const QModelIndex result = find(child.get());
            if (result.isValid()) return result;
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
    QSet<QString> seen;
    QHash<QString, QStringList> grouped;
    for (const Device &d : devices_)
        if (!d.representedByPath.isEmpty()) grouped[d.representedByPath].append(d.path);
    std::function<void(const Node *)> collect = [&](const Node *parent) {
        for (const auto &child : parent->children) {
            if (!child->path.isEmpty() && !seen.contains(child->path)) {
                seen.insert(child->path);
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
    case View::ResourcesByType: return "resourcesByType";
    case View::ResourcesByConnection: return "resourcesByConnection";
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
    case View::ResourcesByType: return tr("Resources by type");
    case View::ResourcesByConnection: return tr("Resources by connection");
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
