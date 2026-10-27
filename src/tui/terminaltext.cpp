#include "terminaltext.h"
#include <QTextBoundaryFinder>
#include <algorithm>
using finalcut::FString;

// Never pass terminal control characters from firmware/udev through to rendering.
FString display(QString value)
{
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        const auto category = c.category();
        if ((category == QChar::Other_Control || category == QChar::Other_Format)
            && c != '\n' && c != '\t')
            value[i] = QChar(0xfffd);
    }
    return FString(value.toStdWString());
}

QStringList wrapTerminalLine(QString line, std::size_t columns)
{
    line.replace('\t', "    ");
    columns = std::max<std::size_t>(2, columns);
    QStringList result;
    while (finalcut::getColumnWidth(display(line)) > columns) {
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, line);
        qsizetype fit = 0, preferred = 0;
        for (int end = boundaries.toNextBoundary(); end >= 0; end = boundaries.toNextBoundary()) {
            if (finalcut::getColumnWidth(display(line.left(end))) > columns - 1) break;
            fit = end;
            const QChar last = line.at(end - 1);
            if (!last.isLetterOrNumber() && last != '_' && !last.isMark()) preferred = end;
        }
        // A grapheme wider than the viewport cannot be split safely.
        if (!fit) { result.append(line); return result; }
        const qsizetype cut = preferred ? preferred : fit;
        result.append(line.left(cut) + QChar(0x25ba));
        line.remove(0, cut);
    }
    result.append(line);
    return result;
}
