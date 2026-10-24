#pragma once

#include "deviceevents.h"
#include <QCoreApplication>
#include <QWidget>

struct Device;
class QLabel;
class QPlainTextEdit;
class QTableWidget;

// Owns retained event rows and selection. Receives history snapshots only;
// never starts monitoring or queries historical logs.
class EventsPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit EventsPage(QWidget *parent = nullptr);
    void updateEvents(const Device &device, const DeviceEventsSnapshot &history,
                      const QString &monitorNote);
    void markRemoved();

private:
    void showEvent();
    void copyEvents(bool selectedOnly);
    QLabel *eventsNotice_ = nullptr;
    QTableWidget *eventsTable_ = nullptr;
    QPlainTextEdit *eventDetails_ = nullptr;
    QVector<DeviceEvent> displayedEvents_;
    QString eventScope_;
    bool removed_ = false;
};
