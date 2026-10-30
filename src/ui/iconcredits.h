#pragma once
#include <QCoreApplication>
#include <QString>

inline QString icons8AttributionHtml()
{
    return QCoreApplication::translate("MainWindow", "Optional Windows 11 Color icons by %1. Copyright Icons8. "
                   "Icons8 assets are excluded from GPLv3. They may be included in this project and downstream builds; "
                   "independent extraction, redistribution or reuse requires a license from Icons8.")
        .toHtmlEscaped().arg(QStringLiteral("<a href=\"https://icons8.com/\">Icons8</a>"));
}
