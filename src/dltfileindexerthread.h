#ifndef DLTFILEINDEXERTHREAD_H
#define DLTFILEINDEXERTHREAD_H

#include "dltfileindexer.h"
#include "dltmsgqueue.h"
#include <QThread>
#include <array>

class DltFileIndexerThread :public QThread
{
    Q_OBJECT
public:
    DltFileIndexerThread(DltFileIndexer *indexer, QDltFilterList *filterList, bool sortByTimeEnabled, bool sortByTimestampEnabled, QVector<qint64> *indexFilterList, QMap<DltFileIndexerKey,qint64> *indexFilterListSorted, QDltPluginManager *pluginManager, QList<QDltPlugin*> *activeViewerPlugins, bool silentMode);
    ~DltFileIndexerThread();
    void enqueueMessage(const QSharedPointer<QDltMsg> &msg, int index);
    void processMessage(QSharedPointer<QDltMsg> &msg, int index);
    void requestStop();

    //! Control-message findings from one message.
    /*! Collected rather than applied directly so the parallel filter pass can
        gather them per chunk and replay them in file order on one thread. */
    struct SideEffects
    {
        QVector<QPair<QString,QString>> versions;        //!< ecuid, version string
        QVector<QPair<int,unsigned char>> timezones;     //!< timezone, isdst
        QVector<std::array<QString,3>> unregisters;      //!< ecuid, apid, ctid
        QVector<int> getLogInfo;                         //!< message indices

        bool isEmpty() const {
            return versions.isEmpty() && timezones.isEmpty() &&
                   unregisters.isEmpty() && getLogInfo.isEmpty();
        }
        void append(const SideEffects &other) {
            versions += other.versions;
            timezones += other.timezones;
            unregisters += other.unregisters;
            getLogInfo += other.getLogInfo;
        }
    };

    //! Extract the control-message side effects of one message.
    static void collectSideEffects(const QDltMsg &msg, int index, SideEffects &out);

protected:
    void run();

private:
    DltFileIndexer *indexer;
    QDltFilterList *filterList;
    bool sortByTimeEnabled;
    bool sortByTimestampEnabled;

    QVector<qint64> *indexFilterList;
    QMap<DltFileIndexerKey,qint64> *indexFilterListSorted;

    QDltPluginManager *pluginManager;
    QList<QDltPlugin*> *activeViewerPlugins;
    bool silentMode;

    DltMsgQueue msgQueue;
};

#endif // DLTFILEINDEXERTHREAD_H
