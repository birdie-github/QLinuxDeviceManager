#include "terminaltext.h"
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
