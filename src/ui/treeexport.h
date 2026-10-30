#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

class QTreeView;
struct TreeExportRow {
    QString label;
    int depth = 0;
    bool branch = false, expanded = false;
    int icon = -1; // Index into the snapshot's deduplicated PNG images.
};
struct TreeExportSnapshot {
    QString view, query, capturedAt;
    // Preorder with nonnegative depths, starting at zero, increasing by at most
    // one after a branch. Includes descendants of collapsed branches.
    QVector<TreeExportRow> rows;
    QVector<QByteArray> icons {}; // Owned PNG bytes; no theme paths or external URLs.
    bool icons8 = false; // Capture selected-theme attribution before the file dialog.
};

QByteArray treeExportText(const TreeExportSnapshot &snapshot);
QByteArray treeExportHtml(const TreeExportSnapshot &snapshot);
// Captures current model labels and resolved icons synchronously, before opening the save dialog.
// No device-property queries, model changes or expansion changes are performed.
void exportDeviceTree(QTreeView *tree, const QString &view, const QString &query, bool icons8);
