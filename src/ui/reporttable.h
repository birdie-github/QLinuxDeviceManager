#pragma once
#include <QTableWidget>

// Read-only report embedded in a scrolling page. The table always exposes all
// rows/columns; its containing page owns scrolling. Callers retain selection policy.
class ReportTable final : public QTableWidget {
public:
    explicit ReportTable(QWidget *parent = nullptr);
    ReportTable(int rows, int columns, QWidget *parent = nullptr);
    void fitContents();
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override;
protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    bool fitQueued_ = false;
    bool fitting_ = false;
    int initializedColumns_ = -1;
};
