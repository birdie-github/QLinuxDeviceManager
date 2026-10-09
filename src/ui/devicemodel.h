#pragma once
#include "device.h"
#include <QAbstractItemModel>
#include <memory>
#include <optional>
#include <vector>

class DeviceModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    enum { PathRole = Qt::UserRole + 1, GenerationRole, CategoryRole };
    explicit DeviceModel(QObject *parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setInventory(QVector<Device> devices);
    std::optional<Device> device(const QString &path, quint64 generation) const;
    void setShowInternal(bool show);
    QModelIndex findDevice(const QString &path, quint64 generation) const;
    int visibleCount() const { return visible_; }
    int inventoryCount() const { return static_cast<int>(devices_.size()); }
private:
    struct Node {
        Node *parent = nullptr;
        int row = 0;
        QString category;
        QString path; // Record lookup ID, never a pointer into inventory storage.
        QString label; // GUI-translated display text, rebuilt with the inventory.
        std::vector<std::unique_ptr<Node>> children;
    };
    Node *node(const QModelIndex &index) const;
    void rebuild();
    QVector<Device> devices_;
    QHash<QString, int> lookup_;
    Node root_;
    bool showInternal_ = false;
    quint64 generation_ = 0;
    int visible_ = 0;
};
