#include "audiopage.h"
#include "audioproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"
#include "reporttable.h"
#include <QApplication>
#include <QClipboard>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>

AudioPage::AudioPage(QWidget *parent) : QWidget(parent)
{
    const auto page = setupPropertyPage(this, tr("ℹ️ Audio card and endpoint snapshot"),
        tr("Read-only information from the current local PulseAudio or PipeWire-Pulse session. "
           "Associated cards include all their endpoints, not only the selected child device. "
           "Configured formats are server reports, not hardware limits or measured audio quality. "
           "No playback, recording, profile or volume changes. Reload properties to refresh."),
        tr("Copy audio properties"));
    groups_ = page.groups;
    connect(page.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void AudioPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void AudioPage::setSnapshot(const AudioProperties &data, const QString &devicePath)
{
    clear();
    QStringList copy {tr("Audio — read-only snapshot"), devicePath};
    PropertyReport report(groups_, copy);
    const auto value = [](const QString &s) { return s.isEmpty() ? tr("Not reported") : s; };
    const auto yesNo = [](bool yes) { return yes ? tr("Yes") : tr("No"); };
    const auto availability = [](int state) {
        return state == 1 ? tr("Unavailable") : state == 2 ? tr("Available") : tr("Unknown");
    };
    auto *session = report.group(tr("Audio session"));
    QString status;
    if (!data.applicable) status = tr("Not collected yet");
    else if (data.status.state == ReadState::Unsupported)
        status = tr("Audio server support was not included in this build");
    else if (data.status.error == ETIMEDOUT) status = tr("Audio server query timed out");
    else if (data.status.error == ECONNREFUSED) status = tr("No accessible audio server in this session");
    else if (data.status.error == EOPNOTSUPP) status = tr("Remote audio servers cannot be matched to local hardware");
    else if (data.status.error == EOVERFLOW) status = tr("Audio report limit exceeded");
    else if (data.status.state == ReadState::PermissionDenied)
        status = tr("Access denied to this session's audio server");
    else if (data.status.state == ReadState::Available) status = tr("Snapshot collected");
    else status = propertyReadValue(data.status);
    report.field(session, tr("Query status"), status, data.diagnostic);
    if (!data.diagnostic.isEmpty()) report.field(session, tr("Server message"), data.diagnostic);
    if (!data.server.isEmpty()) {
        report.field(session, tr("Server"), data.server, "libpulse: pa_server_info.server_name");
        report.field(session, tr("Server-reported version"), value(data.version), "libpulse: pa_server_info.server_version");
    }
    if (data.applicable && data.status.state != ReadState::Available && data.status.state != ReadState::Unsupported) {
        report.field(session, tr("Session access"), tr("Open the application as the desktop user with access to the audio session. "
            "Administrator privileges do not grant access to another user's audio server."));
    } else if (data.cards.isEmpty()) {
        report.field(session, tr("Hardware association"), tr("No audio server card has a matching local sysfs path. "
            "This may be a codec or other device for which the server exposes no separate card."));
    }
    const auto table = [&](const QString &title, const QStringList &headers,
                           const QVector<QStringList> &rows, const QStringList &hints) {
        auto *box = new QGroupBox(title, groups_->parentWidget());
        auto *layout = new QVBoxLayout(box);
        if (rows.isEmpty()) {
            layout->addWidget(plainLabel(tr("No endpoints reported for the active profile"), box));
            copy.append(title + ": " + tr("No endpoints reported for the active profile"));
        } else {
            auto *widget = new ReportTable(static_cast<int>(rows.size()), static_cast<int>(headers.size()), box);
            widget->setHorizontalHeaderLabels(headers);
            layout->addWidget(widget);
            copy.append('\n' + title);
            copy.append(headers.join('\t'));
            for (qsizetype row = 0; row < rows.size(); ++row) {
                copy.append(rows.at(row).join('\t'));
                if (!hints.value(row).isEmpty()) copy.append(hints.at(row));
                for (qsizetype col = 0; col < headers.size(); ++col) {
                    auto *item = new QTableWidgetItem(rows.at(row).value(col));
                    item->setToolTip(propertyToolTip(hints.value(row)));
                    widget->setItem(static_cast<int>(row), static_cast<int>(col), item);
                }
            }
            widget->fitContents();
        }
        groups_->addWidget(box);
    };
    for (const auto &card : data.cards) {
        const QString cardName = card.description.isEmpty() ? value(card.name) : card.description;
        auto *identity = report.group(tr("Associated card — %1").arg(cardName));
        report.field(identity, tr("Card identifier"), value(card.name), "libpulse: pa_card_info.name");
        report.field(identity, tr("Hardware path"), card.path, "libpulse: sysfs.path, canonical local sysfs association");
        report.field(identity, tr("Driver"), value(card.driver), "libpulse: alsa.driver_name");
        report.field(identity, tr("Mixer / codec"), value(card.mixer), "libpulse: alsa.mixer_name");
        report.field(identity, tr("Active profile"), value(card.activeProfile), "libpulse: pa_card_info.active_profile2");
        report.field(identity, tr("Scope"), tr("All endpoints of this associated card. Output monitor sources are omitted."));
        QVector<QStringList> profiles, ports;
        QStringList profileHints, portHints;
        for (const auto &profile : card.profiles) {
            profiles.append({value(profile.description), yesNo(profile.active), yesNo(profile.available),
                QString::number(profile.playback), QString::number(profile.recording)});
            profileHints.append("libpulse: pa_card_info.profiles2\n" + profile.name);
        }
        if (!profiles.isEmpty()) table(tr("Profiles — %1").arg(cardName),
            {tr("Profile"), tr("Active"), tr("Available"), tr("Playback endpoints"), tr("Recording endpoints")}, profiles, profileHints);
        for (const auto &port : card.ports) {
            const QString direction = (port.direction & 3) == 3 ? tr("Playback / Recording")
                : port.direction & 1 ? tr("Playback") : port.direction & 2 ? tr("Recording") : tr("Unknown");
            ports.append({value(port.description), direction, availability(port.availability)});
            portHints.append("libpulse: pa_card_info.ports\n" + port.name);
        }
        if (!ports.isEmpty()) table(tr("Ports — %1").arg(cardName),
            {tr("Port"), tr("Direction"), tr("Availability")}, ports, portHints);
        for (bool input : {false, true}) {
            QVector<QStringList> endpoints;
            QStringList hints;
            for (const auto &endpoint : card.endpoints) {
                if (endpoint.input != input) continue;
                const QString state = endpoint.state == 0 ? tr("Running") : endpoint.state == 1 ? tr("Idle")
                    : endpoint.state == 2 ? tr("Suspended") : tr("Unknown");
                const QString format = endpoint.format.isEmpty() || !endpoint.rate || !endpoint.channels ? tr("Not reported")
                    : tr("%1 · %2 Hz · %3 channels").arg(endpoint.format,
                        QLocale().toString(endpoint.rate), QString::number(endpoint.channels));
                QString portName = endpoint.port;
                for (const auto &port : card.ports)
                    if (port.name == endpoint.port && !port.description.isEmpty()) { portName = port.description; break; }
                endpoints.append({value(endpoint.description.isEmpty() ? endpoint.name : endpoint.description),
                    state, yesNo(endpoint.isDefault), value(portName), format, value(endpoint.channelMap),
                    yesNo(endpoint.muted), endpoint.resolutionBits ? tr("%1 bits").arg(endpoint.resolutionBits) : tr("Not reported")});
                hints.append(QStringLiteral("libpulse: %1\n%2\n").arg(input ? "pa_source_info" : "pa_sink_info", endpoint.name)
                    + tr("Configured format is not a hardware capability limit. Reported resolution is separate "
                         "ALSA metadata and does not establish effective audio quality."));
            }
            table((input ? tr("Recording — %1") : tr("Playback — %1")).arg(cardName),
                {tr("Endpoint"), tr("State"), tr("Default"), tr("Selected port"), tr("Configured format"),
                 tr("Channel layout"), tr("Muted"), tr("Reported resolution")}, endpoints, hints);
        }
    }
    report.alignCaptions();
    copyText_ = copy.join('\n');
}
