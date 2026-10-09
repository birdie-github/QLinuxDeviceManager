#pragma once
#include "device.h"
#include <QThread>
#include <QSet>

struct SystemProperties {
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
};

// Pure parsers shared with fixtures; no commands or shell evaluation.
Attribute parseOsName(const QString &text);
bool parseCpuList(const QString &text, QSet<int> &cpus);
SystemProperties collectSystemProperties();

class SystemPropertiesReader final : public QThread {
public:
    using QThread::QThread;
    SystemProperties takeResult(); // After finished and wait.
protected:
    void run() override { result_ = collectSystemProperties(); }
private:
    SystemProperties result_;
};
