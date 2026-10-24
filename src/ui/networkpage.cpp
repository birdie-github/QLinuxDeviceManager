#include "networkpage.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"

#include <QApplication>
#include <QClipboard>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>

NetworkPage::NetworkPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ Network interface snapshot"),
        tr("Addresses belong to the application's network namespace. Reload properties to refresh. "
        "Hardware MAC is the permanent address reported by the driver. An overridden MAC is the current "
        "address when it differs from the permanent address, or Linux reports a random, inherited or explicitly set address."),
        tr("Copy network properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void NetworkPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void NetworkPage::setSnapshot(const QVector<NetworkProperties> &interfaces,
                              const Attribute &state, const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto ethernetValue = [&](const Attribute &value) {
        if (value.state == ReadState::Unsupported) return tr("Query unsupported by the kernel/device");
        if (value.state == ReadState::Error) {
            if (value.error == EPROTO) return tr("Malformed ethtool reply");
            if (value.error == ETIMEDOUT) return tr("Query timed out");
            if (value.error == EOVERFLOW) return tr("Reply exceeds collection limits");
            if (value.error == EAGAIN) return tr("Link changed during collection; reload properties");
        }
        return propertyReadValue(value);
    };
    if (interfaces.isEmpty()) {
        auto *form = group(tr("Network interfaces"));
        field(form, tr("Interfaces"), state.state == ReadState::Available
            ? tr("No network interfaces observed") : propertyReadValue(state), devicePath);
    }
    for (const NetworkProperties &interface : interfaces) {
        const EthernetProperties &ethernet = interface.ethernet;
        const WirelessProperties &wireless = interface.wireless;
        if (wireless.applicable) {
            auto *form = group(tr("%1 — Wireless").arg(interface.name));
            const auto wifiField = [&](const char *caption, const char *key, const QString &extra = QString()) {
                field(form, QCoreApplication::translate("Wireless", caption),
                    wirelessPropertyText(QLatin1String(key), wireless.values.value(QLatin1String(key))),
                    wireless.sources.value(QLatin1String(key)) + extra);
            };
            field(form, tr("Interface"), tr("802.11 Wi-Fi"), interface.path);
            wifiField(QT_TRANSLATE_NOOP("Wireless", "Operating mode"), "mode");
            if (wireless.values.value("connection").state == ReadState::Available)
                wifiField(QT_TRANSLATE_NOOP("Wireless", "Connection state"), "connection");
            const bool hotspot = wireless.values.value("mode").state == ReadState::Available
                && wireless.values.value("mode").value == "3";
            wifiField(hotspot ? QT_TRANSLATE_NOOP("Wireless", "Hotspot") : QT_TRANSLATE_NOOP("Wireless", "Connected to"), "ssid");
            wifiField(hotspot ? QT_TRANSLATE_NOOP("Wireless", "Hotspot MAC (BSSID)") : QT_TRANSLATE_NOOP("Wireless", "Access point MAC (BSSID)"), "bssid");
            wifiField(QT_TRANSLATE_NOOP("Wireless", "Radio channel"), "frequency");
            wifiField(QT_TRANSLATE_NOOP("Wireless", "Channel width"), "width");
            if (!hotspot) {
                wifiField(QT_TRANSLATE_NOOP("Wireless", "Signal"), "signal");
                if (wireless.values.value("signal").state != ReadState::Available
                    && wireless.values.value("quality").state == ReadState::Available)
                    wifiField(QT_TRANSLATE_NOOP("Wireless", "Signal quality"), "quality");
                wifiField(QT_TRANSLATE_NOOP("Wireless", "Transmit link rate"), "txrate");
                wifiField(QT_TRANSLATE_NOOP("Wireless", "Receive link rate"), "rxrate");
                if (wireless.values.value("nmrate").state == ReadState::Available
                    && wireless.values.value("txrate").state != ReadState::Available
                    && wireless.values.value("rxrate").state != ReadState::Available)
                    wifiField(QT_TRANSLATE_NOOP("Wireless", "Link rate (NetworkManager)"), "nmrate");
                wifiField(QT_TRANSLATE_NOOP("Wireless", "TX spatial streams"), "txstreams");
                wifiField(QT_TRANSLATE_NOOP("Wireless", "RX spatial streams"), "rxstreams");
            }
            wifiField(QT_TRANSLATE_NOOP("Wireless", "Transmit power"), "txpower");
            if (hotspot) wifiField(QT_TRANSLATE_NOOP("Wireless", "Security (configured)"), "securityconfig");
            else wifiField(QT_TRANSLATE_NOOP("Wireless", "Security"), "security");
        }
        if (ethernet.applicable) {
            auto *form = group(tr("%1 — Ethernet link").arg(interface.name));
            const QString source = interface.path + '\n' + ethernet.source;
            const bool disconnected = ethernet.carrier.state == ReadState::Available && ethernet.carrier.value == "0";
            field(form, tr("Link status"), ethernet.carrier.state == ReadState::Available
                ? (disconnected ? tr("Disconnected") : tr("Connected")) : ethernetValue(ethernet.carrier), source);
            const auto operationalValue = [&](const Attribute &value) {
                return value.state == ReadState::NotApplicable && disconnected
                    ? tr("Not applicable — link disconnected") : ethernetValue(value);
            };
            QString speed = operationalValue(ethernet.speed);
            if (ethernet.speed.state == ReadState::Available) {
                bool valid = false;
                const qulonglong mbps = ethernet.speed.value.toULongLong(&valid);
                if (!valid) speed = tr("Malformed ethtool reply");
                else if (!mbps || mbps == 0xffffffffULL) speed = tr("Unavailable (driver reports unknown speed)");
                else speed = mbps >= 1000
                    ? tr("%1 Gbps").arg(QLocale().toString(static_cast<double>(mbps) / 1000, 'g', 6))
                    : tr("%1 Mbps").arg(QLocale().toString(mbps));
            }
            field(form, tr("Link speed"), speed, source + '\n' + tr("Driver-reported operational speed, not measured throughput; it may be forced when autonegotiation is disabled."));
            QString duplex = operationalValue(ethernet.duplex);
            if (ethernet.duplex.state == ReadState::Available) {
                if (ethernet.duplex.value == "0") duplex = tr("Half duplex");
                else if (ethernet.duplex.value == "1") duplex = tr("Full duplex");
                else if (ethernet.duplex.value == "255") duplex = tr("Unavailable (driver reports unknown duplex)");
                else duplex = tr("Unrecognized duplex value: %1").arg(ethernet.duplex.value);
            }
            field(form, tr("Duplex"), duplex, source);
            QString autoneg = ethernetValue(ethernet.autoneg);
            if (ethernet.autoneg.state == ReadState::Available) {
                if (ethernet.autoneg.value == "0") autoneg = tr("Disabled");
                else if (ethernet.autoneg.value == "1") autoneg = tr("Enabled");
                else autoneg = tr("Unrecognized autonegotiation value: %1").arg(ethernet.autoneg.value);
            }
            field(form, tr("Autonegotiation"), autoneg, source);
            field(form, tr("Supported modes / capabilities"), ethernetValue(ethernet.supportedModes).replace('\n', ", "),
                source + '\n' + tr("Kernel-named supported-mask entries include link modes and media, pause or autonegotiation capabilities. Unnamed bits retain their numeric index."));
            QString partner = operationalValue(ethernet.partnerModes);
            if (ethernet.partnerModes.state == ReadState::NotApplicable && !disconnected)
                partner = tr("Not applicable — autonegotiation disabled");
            else if (ethernet.partnerModes.state == ReadState::Unavailable)
                partner = tr("Unavailable — no partner advertisement reported");
            field(form, tr("Link-partner advertised modes"), partner.replace('\n', ", "), source);
        }
        auto *link = group(tr("%1 — Link").arg(interface.name));
        const bool noMac = interface.linkType.state == ReadState::Available && interface.linkType.value != "1";
        const Attribute current = noMac ? Attribute {ReadState::NotApplicable, {}, 0} : interface.currentAddress;
        field(link, tr("Hardware MAC address"), propertyReadValue(interface.permanentAddress),
            tr("Driver permanent address: ETHTOOL_GPERMADDR"));
        field(link, tr("Current MAC address"), propertyReadValue(current), interface.path + "/address");
        QString overrideValue;
        if (noMac) overrideValue = tr("Not applicable");
        else if (current.state != ReadState::Available) overrideValue = propertyReadValue(current);
        else if (interface.permanentAddress.state == ReadState::Available)
            overrideValue = current.value.compare(interface.permanentAddress.value, Qt::CaseInsensitive) == 0
                ? tr("None") : current.value;
        else if (interface.assignmentType.state == ReadState::Available
                 && (interface.assignmentType.value == "1" || interface.assignmentType.value == "2"
                     || interface.assignmentType.value == "3")) overrideValue = current.value;
        else overrideValue = tr("Unavailable; permanent MAC could not be determined");
        field(link, tr("Overridden MAC address"), overrideValue,
            interface.path + "/address; " + interface.path + "/addr_assign_type; ETHTOOL_GPERMADDR");
        field(link, tr("MTU"), propertyReadValue(interface.mtu), interface.path + "/mtu");
        if (!ethernet.applicable && ethernet.scope.state != ReadState::NotApplicable)
            field(link, tr("Ethernet details"), ethernetValue(ethernet.scope), ethernet.source);
        auto *addresses = group(tr("%1 — IP addresses").arg(interface.name));
        const QString source = tr("getifaddrs: application network namespace");
        if (interface.addresses.isEmpty()) {
            field(addresses, tr("IP addresses"), interface.addressState.state == ReadState::Available
                ? tr("None assigned") : propertyReadValue(interface.addressState), source);
        }
        for (const NetworkAddress &address : interface.addresses)
            field(addresses, address.family == "IPv4" ? tr("IPv4 address") : tr("IPv6 address"),
                address.address + '/' + propertyReadValue(address.netmask), source);
    }
    // Match System Information: one font-derived caption width across sections.
    fieldsReport.alignCaptions();
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
