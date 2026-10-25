#pragma once
#include "pages.h"
#include <utility>

// Plain aligned tables using terminal-column widths, including multiline cells.
// Rendering and scrolling stay with FINAL CUT's native text view.
class SystemReport final {
public:
    void fields(const SystemFields &fields);
    void table(const QString &title, const QVector<QStringList> &rows);
    void heading(const QString &text);
    PropertyPage take() { return std::move(result_); }
private:
    PropertyPage result_;
    int line_ = 0;
};
