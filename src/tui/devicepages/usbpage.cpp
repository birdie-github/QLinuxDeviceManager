#include "usbpage.h"
#include "terminaltext.h"

PropertyPage usbPropertyPage(const UsbProperties &snapshot, const QString &path, std::size_t columns)
{
    PropertyPage result;
    const auto append = [&](const PropertyPage &page) {
        const int offset = result.text.count('\n');
        result.text += page.text;
        for (int line : page.shadedLines) result.shadedLines.append(offset + line);
    };
    for (const auto &section : usbPropertySections(snapshot, snapshot.values.value("version"),
                                                 snapshot.values.value("speed"), path)) {
        if (!result.text.isEmpty()) result.text += '\n';
        result.text += section.title + '\n';
        append(propertyFieldsPage(section.fields, columns));
    }
    const QString note = "Power declarations are not measured consumption. Speed is the kernel-reported link rate.";
    result.text += '\n';
    for (const auto &line : columns ? wrapTerminalLine(note, columns) : QStringList{note})
        result.text += line + '\n';
    return result;
}
