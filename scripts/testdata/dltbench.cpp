/* Benchmark harness for the DLT parsing hot paths.
 *
 * Times the two passes that dominate opening a file in the viewer:
 *   1. building the message index (scanning for storage headers)
 *   2. walking every message and evaluating a filter over it
 *
 * Deliberately GUI-free so it can be run from CI or a shell.
 *
 *   dltbench <file.dlt> [repeats]
 *
 * SPDX-License-Identifier: MPL-2.0
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

#include "qdltfile.h"
#include "qdltfilter.h"
#include "qdltfilterlist.h"
#include "qdltfilereader.h"

#include <QtConcurrent>
#include <QFuture>
#include <QThread>

static void report(const char *what, qint64 ms, int messages)
{
    QTextStream out(stdout);
    const double s = ms / 1000.0;
    out << QString("%1 %2 ms").arg(what, -34).arg(ms, 8);
    if (messages > 0 && s > 0.0)
        out << QString("   %1 msg/s").arg(qint64(messages / s), 10);
    out << Qt::endl;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2) {
        out << "usage: dltbench <file.dlt> [repeats]" << Qt::endl;
        return 2;
    }
    const QString filename = QString::fromLocal8Bit(argv[1]);
    const int repeats = (argc > 2) ? atoi(argv[2]) : 1;

    for (int run = 0; run < repeats; run++) {
        QDltFile file;
        file.setDLTv2Support(false);
        if (!file.open(filename)) {
            out << "cannot open " << filename << Qt::endl;
            return 1;
        }

        QElapsedTimer t;

        t.start();
        file.createIndex();
        const qint64 indexMs = t.elapsed();
        const int messages = file.size();

        out << QString("run %1: %2 messages, %3 MiB")
                   .arg(run + 1).arg(messages).arg(file.fileSize() / (1024 * 1024)) << Qt::endl;
        report("  build message index", indexMs, messages);

        /* Pass 2: read and parse every message, as the filter indexer does. */
        t.start();
        QDltMsg msg;
        int parsed = 0;
        for (int i = 0; i < messages; i++) {
            const QByteArray buf = file.getMsg(i);
            if (buf.isEmpty())
                continue;
            if (msg.setMsg(buf, true, false))
                parsed++;
        }
        report("  read + parse every message", t.elapsed(), parsed);

        /* Pass 2b: fetch every message but do not parse it. The gap between
           this and pass 2 is what fusing the filter into the index scan could
           remove: fusing avoids re-fetching each message, but still has to
           parse and match it. */
        t.start();
        qint64 bytes = 0;
        for (int i = 0; i < messages; i++)
            bytes += file.getMsg(i).size();
        report("  fetch only (no parse)", t.elapsed(), messages);
        if (bytes == 0) out << "(no bytes)" << Qt::endl;

        /* Pass 3: the same walk with a payload filter applied, which is what
           applying a filter in the UI actually costs. */
        QDltFilter *filter = new QDltFilter();
        filter->type = QDltFilter::positive;
        filter->enableFilter = true;
        filter->enablePayload = true;
        filter->payload = "NEEDLE";
        QDltFilterList filters;
        filters.addFilter(filter);
        filters.updateSortedFilter();

        t.start();
        int hits = 0;
        for (int i = 0; i < messages; i++) {
            const QByteArray buf = file.getMsg(i);
            if (buf.isEmpty())
                continue;
            if (!msg.setMsg(buf, true, false))
                continue;
            if (filters.checkFilter(msg))
                hits++;
        }
        report("  filter pass (payload match)", t.elapsed(), messages);
        out << QString("    matched %1 messages").arg(hits) << Qt::endl;

        /* Same walk, but a metadata-only filter (APID). This is what a
           precomputed metadata table could answer without touching the file. */
        QDltFilter *mfilter = new QDltFilter();
        mfilter->type = QDltFilter::positive;
        mfilter->enableFilter = true;
        mfilter->enableApid = true;
        mfilter->apid = "APP1";
        QDltFilterList mfilters;
        mfilters.addFilter(mfilter);
        mfilters.updateSortedFilter();

        t.start();
        int mhits = 0;
        for (int i = 0; i < messages; i++) {
            const QByteArray b = file.getMsg(i);
            if (b.isEmpty()) continue;
            if (!msg.setMsg(b, true, false)) continue;
            if (mfilters.checkFilter(msg)) mhits++;
        }
        report("  filter pass (APID match)", t.elapsed(), messages);
        out << QString("    matched %1 messages").arg(mhits) << Qt::endl;

        /* Same APID filter, but split across workers, each with its own
           QDltFileReader so they do not contend on one handle or mutex. */
        const int workers = qMax(1, qMin(QThread::idealThreadCount(), 8));
        const int chunk = (messages + workers - 1) / workers;
        QVector<QVector<qint64>> parts(workers);
        t.start();
        {
            QVector<QFuture<void>> futs;
            for (int w = 0; w < workers; w++) {
                const int from = w * chunk;
                const int to = qMin(from + chunk, messages);
                if (from >= to) continue;
                futs.append(QtConcurrent::run([&, w, from, to]() {
                    QDltFileReader reader(file);
                    QDltMsg m;
                    for (int i = from; i < to; i++) {
                        const QByteArray b = reader.getMsg(i);
                        if (b.isEmpty()) continue;
                        if (!m.setMsg(b, true, false)) continue;
                        if (mfilters.checkFilter(m)) parts[w].append(i);
                    }
                }));
            }
            for (QFuture<void> &f : futs) f.waitForFinished();
        }
        const qint64 parMs = t.elapsed();
        int phits = 0;
        for (const QVector<qint64> &p : parts) phits += p.size();
        report(QString("  filter pass (APID, %1 threads)").arg(workers).toLocal8Bit().constData(), parMs, messages);
        out << QString("    matched %1 messages%2").arg(phits)
                   .arg(phits == mhits ? "  [same as serial]" : "  *** MISMATCH ***") << Qt::endl;
    }
    return 0;
}
