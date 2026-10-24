#pragma once
#include <QString>

// Compare decimal runs by magnitude without integer conversion or locale-dependent
// collation. Device names can contain several runs (e.g. nvme0n10p2).
inline int naturalCompare(const QString &a, const QString &b)
{
    const auto digit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            qsizetype endA = i, endB = j;
            while (endA < a.size() && digit(a[endA])) ++endA;
            while (endB < b.size() && digit(b[endB])) ++endB;
            while (i < endA && a[i] == QLatin1Char('0')) ++i;
            while (j < endB && b[j] == QLatin1Char('0')) ++j;
            if (endA - i != endB - j) return endA - i < endB - j ? -1 : 1;
            while (i < endA) {
                if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
                ++i;
                ++j;
            }
            i = endA;
            j = endB;
        } else {
            const QChar left = a[i].toCaseFolded(), right = b[j].toCaseFolded();
            if (left != right) return left < right ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;
}
