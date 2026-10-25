#include "report.h"
#include "terminaltext.h"
#include <algorithm>

namespace {
// Wrap long prose/features without clipping values or splitting Unicode text.
QStringList wrap(const QString &text)
{
    QStringList result;
    for (QString line : text.split('\n')) {
        while (finalcut::getColumnWidth(display(line)) > 88) {
            int end = std::min<qsizetype>(88, line.size());
            const int space = line.lastIndexOf(' ', end);
            if (space <= 0) break; // Unbroken paths/identifiers scroll horizontally.
            result.append(line.left(space));
            line = line.mid(space + 1);
        }
        result.append(line);
    }
    return result;
}
}
void SystemReport::heading(const QString &text)
{
    if (!result_.text.isEmpty()) { result_.text += '\n'; ++line_; }
    if (!text.isEmpty()) { result_.text += text + '\n'; ++line_; }
}
void SystemReport::fields(const SystemFields &fields)
{
    QVector<QStringList> rows;
    for (const auto &field : fields) rows.append({field.first, field.second});
    table({}, rows);
}
void SystemReport::table(const QString &title, const QVector<QStringList> &rows)
{
    if (!title.isEmpty()) heading(title);
    if (rows.isEmpty()) return;
    qsizetype columns = 0;
    for (const auto &row : rows) columns = std::max(columns, row.size());
    QVector<std::size_t> widths(columns, 0);
    for (const auto &row : rows)
        for (qsizetype c = 0; c < row.size(); ++c)
            for (const auto &line : wrap(row[c]))
                widths[c] = std::max(widths[c], finalcut::getColumnWidth(display(line)));
    for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        QVector<QStringList> cells;
        qsizetype height = 1;
        for (qsizetype c = 0; c < columns; ++c) {
            cells.append(wrap(rows[rowIndex].value(c)));
            height = std::max(height, cells.constLast().size());
        }
        for (qsizetype y = 0; y < height; ++y) {
            for (qsizetype c = 0; c < columns; ++c) {
                const QString cell = cells[c].value(y);
                result_.text += cell;
                if (c + 1 < columns)
                    result_.text += QString(static_cast<qsizetype>(widths[c] - finalcut::getColumnWidth(display(cell))) + 2, QLatin1Char(' '));
            }
            result_.text += '\n';
            if (rowIndex % 2) result_.shadedLines.append(line_);
            ++line_;
        }
    }
}
