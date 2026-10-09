#pragma once
#include "deviceproperties.h"

struct SearchRecord {
    Device device;
    QString label; // Exact label currently presented by the source model.
    QString groupedPaths;
};
struct SearchField { QString label; QString value; };
using SearchDocument = QVector<SearchField>;
struct SearchMatch { QString path; quint64 generation = 0; QString field; };
struct SearchBatch {
    quint64 request = 0;
    QVector<SearchMatch> matches;
    int done = 0;
    int total = 0;
    int unavailable = 0;
};
Q_DECLARE_METATYPE(SearchBatch)

SearchDocument deviceSearchDocument(const SearchRecord &record, const DeviceProperties &properties);
QString searchDocumentMatch(const SearchDocument &document, const QString &query);

// One serial worker. All records/results are owned; cache is worker-thread-only.
class DeepSearchWorker final : public QThread {
    Q_OBJECT
public:
    explicit DeepSearchWorker(QObject *parent = nullptr) : QThread(parent) {}
    void setRequest(QVector<SearchRecord> records, QString query, quint64 revision, quint64 request);
signals:
    void batchReady(SearchBatch batch);
protected:
    void run() override;
private:
    struct Cached { quint64 generation; SearchDocument document; bool unavailable; };
    QVector<SearchRecord> records_;
    QString query_;
    quint64 revision_ = 0;
    quint64 cachedRevision_ = 0;
    quint64 request_ = 0;
    QHash<QString, Cached> cache_;
    qsizetype cacheBytes_ = 0;
};
