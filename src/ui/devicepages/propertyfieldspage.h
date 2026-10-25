#pragma once
#include <QScrollArea>
#include <QVector>

class QFormLayout;
struct PropertyEntry;

// General and Driver share a read-only form, supplied from the same entries as
// Details. The page owns its widgets and never retains caller-owned entries.
class PropertyFieldsPage final : public QScrollArea {
public:
    enum class Section { General = 0, Driver = 1 };
    explicit PropertyFieldsPage(Section section, QWidget *parent = nullptr);
    void setSnapshot(const QVector<PropertyEntry> &entries);
private:
    Section section_;
    QFormLayout *form_ = nullptr;
};
