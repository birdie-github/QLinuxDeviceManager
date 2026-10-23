#include "gpulinktest.h"
#include <QCoreApplication>
#include <QProcess>
#include <QPointer>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>

bool gpuLinkTestBuilt()
{
#ifdef QLDM_HAVE_GPU_LINK_TEST
    return true;
#else
    return false;
#endif
}

QProcess *startGpuLinkTest(const Device &device, QObject *owner)
{
    static QPointer<QProcess> active;
    if (active && active->state() != QProcess::NotRunning) return nullptr;
    auto *process = new QProcess(QCoreApplication::instance());
    active = process;
    QObject::connect(owner, &QObject::destroyed, process, [process] { process->kill(); });
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({QStringLiteral("--gpu-link-test"), device.path, device.incarnation});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const pid_t parentPid = getpid();
    process->setChildProcessModifier([parentPid] {
        // No orphan workload if the viewer exits unexpectedly. Only system
        // calls here: Qt forbids ordinary Qt work between fork and exec.
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parentPid) _exit(127);
    });
    process->start();
    return process;
}

#ifndef QLDM_HAVE_GPU_LINK_TEST
int runGpuLinkTest(const QString &, const QString &) { return 1; }
#endif
