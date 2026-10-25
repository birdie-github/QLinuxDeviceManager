#pragma once
#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct AudioProperties;

// Presentation only: renders an owned collector snapshot, with no server access.
class AudioPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(AudioPage)
public:
    explicit AudioPage(QWidget *parent = nullptr);
    void setSnapshot(const AudioProperties &snapshot, const QString &devicePath);
    void clear();
private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
