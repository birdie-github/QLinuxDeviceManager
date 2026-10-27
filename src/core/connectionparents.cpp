#include "inventory.h"

QHash<QString, QString> connectionParents(const QVector<Device> &devices)
{
    QSet<QString> paths;
    for (const Device &d : devices) paths.insert(d.path);
    QHash<QString, QString> parents;
    for (const Device &d : devices) {
        QString path = d.parentPath;
        if (!paths.contains(path) && path.startsWith("/sys/devices/")
            && d.path.startsWith(path + '/')) {
            while (path.startsWith("/sys/devices/") && !paths.contains(path))
                path.truncate(path.lastIndexOf('/'));
            if (!paths.contains(path)) path.clear();
        }
        parents.insert(d.path, path);
    }
    return parents;
}
