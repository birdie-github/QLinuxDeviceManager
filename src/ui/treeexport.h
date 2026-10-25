#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

class QTreeView;
struct TreeExportRow {
    QString label;
    int depth = 0;
    bool branch = false, expanded = false;
};
struct TreeExportSnapshot {
    QString view, query, capturedAt;
    // Preorder with nonnegative depths, starting at zero, increasing by at most
    // one after a branch. Includes descendants of collapsed branches.
    QVector<TreeExportRow> rows;
};

QByteArray treeExportText(const TreeExportSnapshot &snapshot);
QByteArray treeExportHtml(const TreeExportSnapshot &snapshot);
// Captures current model labels synchronously, before opening the save dialog.
// No device-property queries, model changes or expansion changes are performed.
void exportDeviceTree(QTreeView *tree, const QString &view, const QString &query);
