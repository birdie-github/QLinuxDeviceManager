#pragma once
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QStringList>
#include <QStyle>

struct Device;

// GUI-thread only. Theme PNGs are decoded once on selection, never in model data().
class IconTheme final : public QObject {
public:
    static IconTheme &instance();
    QStringList availableThemes() const;
    QString theme() const { return theme_; }
    void setTheme(const QString &name);
    QIcon icon(const QString &role, const QString &systemName,
               QStyle::StandardPixmap fallback, const QString &categoryRole = {}) const;
    static QString categoryRole(const QString &category);
    static QString deviceRole(const Device &device);
private:
    explicit IconTheme(QObject *parent) : QObject(parent) {}
    QString theme_;
    QHash<QString, QIcon> icons_;
};
