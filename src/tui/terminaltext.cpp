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
        qsizetype fit = 0, whitespace = 0, punctuation = 0;
        for (int end = boundaries.toNextBoundary(); end >= 0; end = boundaries.toNextBoundary()) {
            if (finalcut::getColumnWidth(display(line.left(end))) > columns - 1) break;
            fit = end;
            const QChar last = line.at(end - 1);
            if (last.isSpace()) whitespace = end;
            else if (!last.isLetterOrNumber() && last != '_' && !last.isMark()
                     && last.category() != QChar::Punctuation_Open)
                punctuation = end;
        }
        // A grapheme wider than the viewport cannot be split safely.
        if (!fit) { result.append(line); return result; }
        // Keep space-delimited values (e.g. [0000:00:14.3]) together first.
        const qsizetype cut = whitespace ? whitespace : punctuation ? punctuation : fit;
        result.append(line.left(cut) + QChar(0x25ba));
        line.remove(0, cut);
    }
    result.append(line);
    return result;
}
