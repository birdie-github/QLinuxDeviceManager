#include "functionnames.h"
#include <QCoreApplication>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <utility>

namespace {
QHash<QString, QString> acpiNames;
QString acpiNamesPath;
}

bool loadAcpiFunctionNames(const QString &path, QString &error)
{
    constexpr qint64 maximumSize = 2 * 1024 * 1024;
    const auto fail = [&](const QString &reason) {
        error = QStringLiteral("%1: %2").arg(path, reason);
        return false;
    };
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return fail(QString::fromLocal8Bit(std::strerror(errno)));
    struct stat info {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > maximumSize) {
        close(fd);
        return fail(QStringLiteral("expected a regular file no larger than 2 MiB"));
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        close(fd);
        return fail(file.errorString());
    }
    const QByteArray bytes = file.read(maximumSize + 1);
    if (file.error() != QFileDevice::NoError) return fail(file.errorString());
    if (bytes.size() > maximumSize) return fail(QStringLiteral("file exceeds 2 MiB"));
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString contents = decoder(bytes);
    if (decoder.hasError()) return fail(QStringLiteral("invalid UTF-8"));
    const QRegularExpression identifier(QStringLiteral("^[A-Z0-9_]{3,32}$"));
    const QRegularExpression controlCharacter(QStringLiteral("\\p{Cc}"));
    QHash<QString, QString> names;
    int lineNumber = 0;
    for (const QString &line : contents.split('\n')) {
        ++lineNumber;
        if (line.trimmed().isEmpty() || line.trimmed().startsWith('#')) continue;
        const QStringList fields = line.split('\t');
        if (fields.size() != 2 || !identifier.match(fields[0]).hasMatch()
            || fields[1].trimmed().isEmpty() || fields[1].size() > 512
            || controlCharacter.match(fields[1].trimmed()).hasMatch()
            || names.contains(fields[0]) || names.size() >= 32768)
            return fail(QStringLiteral("invalid or duplicate entry on line %1").arg(lineNumber));
        names.insert(fields[0], fields[1].trimmed());
    }
    if (names.isEmpty()) return fail(QStringLiteral("no device names"));
    acpiNames = std::move(names);
    acpiNamesPath = QFileInfo(path).absoluteFilePath();
    error.clear();
    return true;
}

FunctionName knownFunctionName(const Device &d)
{
    struct Entry { const char *id; const char *label; };
    if (d.subsystem == "usb" && d.devtype == "usb_device" && d.sysname.startsWith("usb")) {
        bool busValid = false, addressValid = false, vendorValid = false, productValid = false;
        const uint bus = d.sysname.mid(3).toUInt(&busValid);
        const uint address = d.properties.value("DEVNUM").toUInt(&addressValid);
        const QStringList product = d.properties.value("PRODUCT").split('/');
        if (product.size() == 3 && busValid && bus > 0 && addressValid && address == 1) {
            const uint vendor = product[0].toUInt(&vendorValid, 16);
            const uint id = product[1].toUInt(&productValid, 16);
            // Linux USB core's root-hub descriptor IDs; external hubs have port names.
            if (vendorValid && productValid && vendor == 0x1d6b) {
                const char *label = nullptr;
                if (id == 1) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 1.1 root hub");
                else if (id == 2) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 2.0 root hub");
                else if (id == 3) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 3.x root hub");
                if (label) return {QString::fromLatin1(label), "Linux USB root-hub topology and descriptor identity"};
            }
        }
    }
    const QString alias = d.properties.value("MODALIAS");
    QStringList identifiers;
    if ((d.subsystem == "acpi" || d.subsystem == "platform" || d.subsystem == "i2c") && alias.startsWith("acpi:"))
        identifiers = alias.mid(5).split(':', Qt::SkipEmptyParts);
    else if (d.subsystem == "pnp" && alias.startsWith("pnp:")) {
        // PNP modaliases encode each identifier as d<ID>, including compatible IDs.
        for (const auto &id : alias.mid(4).split('d', Qt::SkipEmptyParts)) identifiers.append(id);
    }
    for (const auto &id : identifiers) {
        const auto found = acpiNames.constFind(id.toUpper());
        if (found != acpiNames.cend())
            return {found.value(), QStringLiteral("%1 : ACPI/PNP ID: %2").arg(acpiNamesPath, id)};
    }
    static const Entry platformAliases[] = {
        // Linux drivers/mailbox/pcc.c registers the PCCT mailbox controller.
        {"platform:PCCT", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI platform communications channel")},
        {"platform:pcspkr", QT_TRANSLATE_NOOP("DeviceFunctions", "PC speaker (beeper interface)")},
        {"platform:coretemp", QT_TRANSLATE_NOOP("DeviceFunctions", "Intel CPU temperature monitor")},
        {"platform:rtc-efi", QT_TRANSLATE_NOOP("DeviceFunctions", "EFI real-time clock interface")},
        {"platform:alarmtimer", QT_TRANSLATE_NOOP("DeviceFunctions", "Kernel alarm timer")}
    };
    if (d.subsystem == "platform")
        for (const auto &entry : platformAliases)
            if (alias == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("documented kernel platform alias: %1").arg(alias)};
    static const Entry pciServices[] = {
        {"aer", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express advanced error reporting service")},
        {"pcie_bwctrl", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express bandwidth control service")},
        {"pcie_pme", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express power-management event service")},
        {"pciehp", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express hot-plug service")}
    };
    if (d.subsystem == "pci_express")
        for (const auto &entry : pciServices)
            if (d.driver == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("PCI Express service; direct driver: %1").arg(d.driver)};
    static const Entry fauxFunctions[] = {
        {"microcode", QT_TRANSLATE_NOOP("DeviceFunctions", "CPU microcode interface")},
        {"reg-dummy", QT_TRANSLATE_NOOP("DeviceFunctions", "Dummy voltage regulator")},
        {"regulatory", QT_TRANSLATE_NOOP("DeviceFunctions", "Wireless regulatory interface")},
        {"snd-soc-dummy", QT_TRANSLATE_NOOP("DeviceFunctions", "Dummy SoC audio component")}
    };
    if (d.subsystem == "faux")
        for (const auto &entry : fauxFunctions)
            if (d.sysname == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("documented kernel faux function: %1").arg(d.sysname)};
    // A dummy client claims an address for a driver; it does not identify a chip.
    // Linux i2c_new_dummy_device() uses the dummy type for secondary addresses.
    if (d.subsystem == "i2c" && d.driver == "dummy" && alias == "i2c:dummy")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "I²C address placeholder")),
            "I2C dummy client alias and direct dummy binding"};
    if (d.subsystem == "serial-base" && d.driver == "port")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial port function")), "serial-base subsystem; direct port binding"};
    if (d.subsystem == "serial-base" && d.driver == "ctrl")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial controller function")), "serial-base subsystem; direct ctrl binding"};
    if (d.subsystem == "bluetooth" && d.sysname.startsWith("hci") && !d.sysname.contains(':'))
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Bluetooth host controller")), "Bluetooth subsystem; host-controller interface"};
    return {};
}
