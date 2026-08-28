#include "dltfileindexer.h"
#include "dltfileindexerthread.h"
#include "dltfileindexerdefaultfilterthread.h"

#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <QTime>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <vector>
#include <limits>
#include <atomic>
#include <QtConcurrent>
#include <QFuture>

#include "qdltfilereader.h"
#include "dltfileindexerthread.h"

#include "qdltoptmanager.h"

extern "C" {
    #include "dlt_common.h"
}

DltFileIndexerKey::DltFileIndexerKey(time_t time, unsigned int microseconds, int index)
    : timestamp(0)
{
    this->time = time;
    this->microseconds = microseconds;
    this->index = index;
}

DltFileIndexerKey::DltFileIndexerKey(unsigned int timestamp, int index)
    : time(0)
    , microseconds(0)
{
    this->timestamp = timestamp;
    this->index = index;
}

DltFileIndexer::DltFileIndexer(QObject *parent) :
    QThread(parent)
{
    mode = modeIndexAndFilter;
    this->dltFile = NULL;
    this->pluginManager = NULL;
    defaultFilter = NULL;
    stopFlag = 0;

    pluginsEnabled = true;
    filtersEnabled = true;
    multithreaded = true;
    sortByTimeEnabled = false;
    sortByTimestampEnabled = false;

    maxRun = 0;
    currentRun = 0;
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;

    filterIndexEnabled = false;
    filterIndexStart = 0;
    filterIndexEnd = 0;
}

DltFileIndexer::DltFileIndexer(QDltFile *dltFile, QDltPluginManager *pluginManager, QDltDefaultFilter *defaultFilter, QMainWindow *parent) :
    QThread(parent)
{
    mode = modeIndexAndFilter;
    this->dltFile = dltFile;
    this->pluginManager = pluginManager;
    this->defaultFilter = defaultFilter;
    stopFlag = 0;

    pluginsEnabled = true;
    filtersEnabled = true;
    multithreaded = true;
    sortByTimeEnabled = 0;
    sortByTimestampEnabled = 0;
    errors_in_file  = 0;

    maxRun = 0;
    currentRun = 0;
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;

    filterIndexEnabled = false;
    filterIndexStart = 0;
    filterIndexEnd = 0;
}

DltFileIndexer::~DltFileIndexer()
{
}

bool DltFileIndexer::index(int num)
{
    if (!dltFile)
    {
        qWarning() << "DltFileIndexer::index called with null dltFile";
        return false;
    }

    // load filter index if enabled
    if(filterCacheEnabled && loadIndexCache(dltFile->getFileName(num)))
    {
        // loading index from filter is successful
        qDebug() << "Successfully loaded index cache for file" << dltFile->getFileName(num);// << __LINE__;
        return true;
    }

    // prepare indexing
    QFile f(dltFile->getFileName(num));

    // open file
    if(!f.open(QIODevice::ReadOnly))
    {
        qWarning() << "Cannot open file in DltFileIndexer " << f.errorString();
        return false;
    }

    // clear old index
    indexAllList.clear();

    // Pre-size the index from an estimated average message size. This is a
    // hint, not a bound: a smaller average simply costs a few reallocations,
    // while a too-generous one wastes real memory (each entry is 8 bytes, and
    // a large file has millions of them). 128 bytes is a realistic average for
    // DLT traffic -- a 16-byte storage header, a 4..14 byte header and a
    // payload -- and keeps the estimate on the conservative side.
    if(f.size() > 0)
    {
        indexAllList.reserve(static_cast<qsizetype>(f.size() / 128) + 1);
    }

    // check if file is empty
    if(f.size() <= 0)
    {
        // No need to do anything here.
        f.close();
        qWarning() << "File" << dltFile->getFileName(num) << "is empty";
        return true; // because it is just empty, not an error ...
    }

    int modulo = f.size()/200; // seems to be the propper ratio ...
    if (modulo == 0) // avoid divison by zero ( very small files )
    {
         modulo = 1;
    }

    qDebug() << "Start creating indexfile for" << dltFile->getFileName(num);

    // Go through the segments and create new index
    char lastFound = 0;
    qint64 length = 0;
    qint64 msgindex = 0;
    qint64 pos = 0;
    qint64 current_message_pos = 0;
    qint64 next_message_pos = 0;
    qint64 counter_header = 0;
    qint64 message_length = 0;
    qint64 readresult = 0;
    qint64 file_size = f.size();
    qint64 number=0;
    quint8 version=1;
    qint64 lengthOffset=2;
    qint64 storageLength=0;
    errors_in_file  = 0;
    std::vector<char> data(DLT_FILE_INDEXER_SEG_SIZE);

    // Initialise progress bar
    emit(progressText(QString("CI %1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(100));
    emit(progress(0));

    unsigned int progressCounter = 1;
    unsigned int percent = 0;
    unsigned long fileSize = f.size();

    qDebug() << "Create index: Start";
    do
    {
        pos = f.pos();
        readresult = f.read(data.data(), static_cast<qint64>(data.size()));
        if (length >= 0)
        {
           length = readresult;
        }
        else
        {
            qDebug() << "Error reading input file" << f.fileName() << __LINE__;
            f.close();
            return false;
        }

        for(number=0;number < length;number++)
        {
            // search length of DLT message
            if(counter_header>0)
            {
                counter_header++;
                if(storageLength==13 && counter_header==13)
                {
                    storageLength += ((unsigned char)data[number]) + 1;
                }
                else if (counter_header==storageLength)
                {
                    // Read DLT protocol version
                    version = (((unsigned char)data[number])&0xe0)>>5;
                    if(version==1)
                    {
                        lengthOffset = 2;
                    }
                    else if(version==2)
                    {
                        lengthOffset = 5;
                    }
                    else
                    {
                        lengthOffset = 2;  // default
                    }
                }
                else if (counter_header==storageLength+lengthOffset) // was 16
                {
                    // Read low byte of message length
                    message_length = (unsigned char)data[number];
                }
                else if (counter_header==storageLength+1+lengthOffset) // was 17
                {
                    // Read high byte of message length
                    counter_header = 0;
                    message_length = (message_length<<8 | ((unsigned char)data[number])) + storageLength;
                    next_message_pos = current_message_pos + message_length;
                    if(next_message_pos==file_size)
                    {
                        // last message found in file
                        indexAllList.append(current_message_pos);
                        break;
                    }
                    // speed up move directly to next message, if inside current buffer
                    if((message_length > (storageLength+2+lengthOffset))) // was 20
                    {
                        if((number+message_length-(storageLength+2+lengthOffset)<length))  // was 20
                        {
                            number+=message_length-(storageLength+2+lengthOffset);  // was 20
                        }
                    }
                }
            }
            // find DLT Header
            else if(data[number] == 'D')
            {
                lastFound = 'D';
            }
            else if(lastFound == 'D' && data[number] == 'L')
            {
                lastFound = 'L';
            }
            else if(lastFound == 'L' && data[number] == 'T')
            {
                lastFound = 'T';
            }
            else if(lastFound == 'T' && (data[number] == 0x01 || data[number] == 0x02))
            {
                if(next_message_pos == 0)
                {
                    // very first message detected or the first message after an error occurred
                    current_message_pos = pos+number-3;
                    counter_header = 3;
                    if(data[number] == 0x01)
                        storageLength = 16;
                    else
                        storageLength = 13;
                    if(current_message_pos!=0)
                    {
                        // first messages not at beginning or error occurred before
                        errors_in_file++;
                        qDebug() << "ERROR in file" << dltFile->getFileName(num) << "detected new start sequence at index" << msgindex << "msg length" << message_length << "file position" << current_message_pos;
                        qDebug() << "------------";
                    }
                    // speed up and move directly to message length, if it is still inside of the current buffer
                    if(number+9<length)
                    {
                        number+=9;
                        counter_header+=9;
                    }
                }
                else if( next_message_pos == (pos+number-3) )
                {
                    // Add message only when it is in the correct position in relationship to the last message
                    indexAllList.append(current_message_pos);
                    msgindex++;
                    current_message_pos = pos+number-3;
                    counter_header = 3;
                    if(data[number] == 0x01)
                        storageLength = 16;
                    else
                        storageLength = 13;
                    // speed up move directly to message length, if inside current buffer
                    //if ( (errors_in_file > 0)  &&  ((pos%1000)) )    qDebug() << "Add index "<< msgindex << "at file position" << current_message_pos << pos << number << length;

                    if(number+9 < length)
                    {
                        number+=9;
                        counter_header+=9;
                    }
                }
                else if(next_message_pos > (pos+number-3))
                {
                    // Header detected before end of message
                     qDebug() << "ERROR: Header detected before end of message at index "<< msgindex << "msg length" << message_length << "at file position" << current_message_pos;
                     errors_in_file++;
                }
                else //if(next_message_pos < (pos+number-3))
                {
                    // Header detected after end of message
                    // start search for new message back after last header found
                    qDebug() << "At index file:" << ( pos *100 )/file_size << "% -" << "Header detected after end of message, offset:" << (pos+number-3) - next_message_pos << "bytes";
                    f.seek(current_message_pos+4);
                    pos = current_message_pos+4;
                    length = f.read(data.data(), static_cast<qint64>(data.size()));
                    number=0;
                    next_message_pos = 0;
                }
                lastFound = 0;
            }
            else
            {
                lastFound = 0; // no hit, so just go on with search for the startsequence
                //qDebug() << "DLT recived but not the stop sign 0x01" << msgindex;
            }
        } // end of for loop to read within one segment accross "number"

        // Progress and cancellation are handled once per ~1 MB segment rather
        // than once per byte examined. The old placement inside the inner loop
        // cost a QFile::pos() virtual call plus a 64-bit division for every
        // iteration, which is roughly a dozen times per DLT message.
        /* stop if requested */
        if(true == stopFlag)
        {
            qDebug().noquote() << "Request stoping indexing received" << __LINE__ << __FILE__;
            emit(progress(pos));
            f.close();
            return false;
        }

        if(fileSize)
            percent = ((pos + length) * 100) / fileSize;

        if(percent>=progressCounter)
        {
            progressCounter = percent + 1;
            emit(progress(percent));
            if((percent>0) && ((percent%10)==0))
                qDebug() << "CI:" << percent << "%";
        }
    }
    while(length>0); // overall "do loop"
    qDebug() << "Create index: Finish";

    if ( errors_in_file != 0 )
    {
    qDebug() << "Indexing error:" << errors_in_file << "wrong DLT message headers found during indexing" << msgindex << "messages";
    }

    if ( file_size > 0 )
    {
     qDebug().noquote() << "Created" << ( pos *100 )/file_size << "% index for file" << dltFile->getFileName(num);
    }

    // write index if enabled
    if(filterCacheEnabled)
    {
        saveIndexCache(dltFile->getFileName(num));
        qDebug() << "Saved index cache for file" << dltFile->getFileName(num);
    }
    emit(progress(pos));

    // buffer is RAII-managed by std::vector

    // close file
    f.close();

    //qDebug() << "Duration:" << time.elapsed()/1000 << __LINE__;

    return true;
}

bool DltFileIndexer::indexFilter(QStringList filenames)
{
    if (!dltFile || !pluginManager)
    {
        qWarning() << "DltFileIndexer::indexFilter called with null dltFile or pluginManager";
        return false;
    }

    QSharedPointer<QDltMsg> msg;
    QDltFilterList filterList;
    quint64 ix = 0;
    unsigned int iPercent = 0;

    // get filter list
    filterList = dltFile->getFilterList();
    resetMarkerCounts(filterList);
    // clear index filter
    indexFilterList.clear();
    indexFilterListSorted.clear();
    getLogInfoList.clear();

    // calculate start and end index
    quint64 start,end;
    if(!filterIndexEnabled)
    {
        start = 0;
        end = dltFile->size();
    }
    else
    {
        if(filterIndexStart<=dltFile->size())
            start = filterIndexStart;
        else
            start = 0;
        if(filterIndexEnd<=dltFile->size())
            end = filterIndexEnd + 1;
        else
            end = dltFile->size();
        if(start>end)
            start=end;
    }

    /* In-memory result of the last filter pass.
     *
     * Switching filters off and back on is a normal way to look at the context
     * around a hit, and it recomputed the whole pass every time unless the
     * on-disk index cache happened to be enabled. The key is the same one the
     * disk cache uses, so it already covers the filter set, the files and their
     * size, the active decoder plugins, the sort mode and the filter range.
     *
     * Only used for modeFilter: a fresh load still has to walk the file to
     * collect the control-message side effects. */
    if(mode == modeFilter && memoFilterValid && !memoFilterKey.isEmpty())
    {
        const QString key = filenameFilterIndexCache(filterList, filenames);
        if(key == memoFilterKey)
        {
            indexFilterList = memoFilterIndex;
            qDebug() << "Reused the previous filter index:" << indexFilterList.size() << "messages";
            computeMarkerCountsFromIndex(filterList, &indexFilterList);
            emit(progress(100));
            return true;
        }
    }

    // load filter index, if enabled and not an initial loading of file
    if(filterCacheEnabled && mode != modeIndexAndFilter && loadFilterIndexCache(filterList,indexFilterList,filenames))
    {
        // loading filter index from filter is successful
        qDebug() << "Loaded filter index cache for files" << filenames;
        computeMarkerCountsFromIndex(filterList, &indexFilterList);
        return true;
    }

    // check if file is empty

    if(dltFile->size() == 0)
    {
        // No need to do anything here.
        return true;
    }

    // Initialise progress bar
    emit(progressText(QString("CFI %1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(100));
    emit(progress(0));

    // get silent mode
    bool silentMode = !QDltOptManager::getInstance()->issilentMode();

    //bool hasPlugins = (activeDecoderPlugins.size() + activeViewerPlugins.size()) > 0;
    //bool hasFilters = filterList.filters.size() > 0;

    //bool useIndexerThread = hasPlugins || hasFilters;

    DltFileIndexerThread indexerThread
            (
                this,
                &filterList,
                sortByTimeEnabled,
                sortByTimestampEnabled,
                &indexFilterList,
                &indexFilterListSorted,
                pluginManager,
                &activeViewerPlugins,
                silentMode
            );

    /*if(useIndexerThread)
    {
        indexerThread.start(); // thread starts reading its queue
    }*/

    qDebug() << "### Create filter index";
    qDebug() << "Create filter index: Start";

    /* init fileprogress */
    unsigned int progressCounter = 1;
    emit progress(0);

    /* Parallel filter pass.
     *
     * Deciding whether a message matches is independent per message, and on the
     * measured 500 MiB sample the match itself is ~2% of the cost -- the rest is
     * fetching and parsing. That is embarrassingly parallel, but QDltFile
     * serialises every fetch on one mutex and one file handle, so each worker
     * gets its own QDltFileReader instead.
     *
     * The serial path is kept for the cases the parallel one cannot serve:
     *  - viewer/decoder plugins, whose decodeMsg() holds a global lock and whose
     *    thread safety is not guaranteed,
     *  - sorted output, which accumulates into a shared QMap,
     *  - viewer plugins, which must see every message in file order,
     *  - small files, where the thread setup costs more than it saves.
     *
     * The control-message side effects that modeIndexAndFilter collects
     * (software version, timezone, unregister-context, GetLogInfo) are gathered
     * per chunk and replayed in file order below, so the initial load can use
     * this path too.
     */
    const bool canRunParallel =
        (mode == DltFileIndexer::modeFilter || mode == DltFileIndexer::modeIndexAndFilter) &&
        !sortByTimeEnabled && !sortByTimestampEnabled &&
        (!getDecoderPluginsActive() || (pluginManager && pluginManager->decodersAreReentrant())) &&
        activeViewerPlugins.isEmpty() &&
        ((end - start) >= 200000);

    if(canRunParallel)
    {
        const int workers = qMax(1, qMin(QThread::idealThreadCount(), 8));
        const quint64 span = end - start;
        const quint64 chunk = (span + workers - 1) / workers;

        qDebug() << "Create filter index: parallel over" << workers << "workers";

        QVector<QVector<qint64>> results(workers);
        QVector<DltFileIndexerThread::SideEffects> effects(workers);
        QVector<QFuture<void>> futures;
        futures.reserve(workers);

        QDltFile *fileForWorkers = dltFile;
        QDltFilterList *filtersForWorkers = &filterList;
        const bool dltv2 = dltFile->getDLTv2Support();
        const bool collectEffects = (mode == DltFileIndexer::modeIndexAndFilter);
        const bool decodeInWorkers = pluginsEnabled && pluginManager && getDecoderPluginsActive();
        QDltPluginManager *pluginsForWorkers = pluginManager;
        std::atomic<int> done{0};
        std::atomic<quint64> processed{0};   //!< messages examined, for the progress bar

        for(int w = 0; w < workers; w++)
        {
            const quint64 from = start + (quint64)w * chunk;
            const quint64 to = qMin<quint64>(from + chunk, end);
            if(from >= to)
                continue;

            futures.append(QtConcurrent::run([=, &results, &effects, &done, &processed]() {
                QDltFileReader reader(*fileForWorkers);
                QVector<qint64> &out = results[w];
                out.reserve(static_cast<qsizetype>((to - from) / 8));
                QDltMsg msg;
                for(quint64 i = from; i < to; i++)
                {
                    if((i - from) % 4096 == 4095)
                        processed.fetch_add(4096, std::memory_order_relaxed);
                    if(stopFlag)
                        break;
                    const QByteArray buf = reader.getMsg(static_cast<int>(i));
                    if(buf.isEmpty())
                        continue;
                    if(!msg.setMsg(buf, true, dltv2))
                        continue;
                    msg.setIndex(static_cast<int>(i));
                    if(collectEffects)
                        DltFileIndexerThread::collectSideEffects(msg, static_cast<int>(i), effects[w]);
                    if(decodeInWorkers)
                        pluginsForWorkers->decodeMsg(msg, silentMode);
                    if(filtersForWorkers->checkFilter(msg))
                        out.append(static_cast<qint64>(i));
                }
                done.fetch_add(1);
            }));
        }

        /* Keep the progress bar moving without blocking the workers. Report
           messages examined rather than workers finished: with 8 chunks that
           only ever produced 0, 12, 25 ... and in practice looked like a jump
           straight from 0 to 100. */
        unsigned int lastPercent = 0;
        unsigned int nextLogMark = 10;
        while(true)
        {
            bool allDone = true;
            for(const QFuture<void> &f : futures)
                if(!f.isFinished()) { allDone = false; break; }
            if(allDone)
                break;

            const quint64 seen = processed.load(std::memory_order_relaxed);
            const unsigned int percent =
                (span > 0) ? static_cast<unsigned int>(qMin<quint64>(99, (seen * 100) / span)) : 99;
            if(percent != lastPercent)
            {
                lastPercent = percent;
                emit progress(percent);
                if(percent >= nextLogMark)
                {
                    qDebug() << "CFI:" << percent << "%";
                    nextLogMark = ((percent / 10) + 1) * 10;
                }
            }
            QThread::msleep(25);
        }
        for(QFuture<void> &f : futures)
            f.waitForFinished();

        if(stopFlag)
            return false;

        /* concatenate in chunk order: each chunk is already ascending, and the
           chunks are contiguous, so the result is sorted by construction */
        qsizetype total = 0;
        for(const QVector<qint64> &r : results)
            total += r.size();
        indexFilterList.reserve(total);
        for(const QVector<qint64> &r : results)
            indexFilterList.append(r);

        /* Replay the control-message side effects in file order, on this
           thread, so the receivers see exactly what the serial path emits. */
        if(collectEffects)
        {
            DltFileIndexerThread::SideEffects merged;
            for(const DltFileIndexerThread::SideEffects &e : effects)
                merged.append(e);

            for(const auto &v : merged.versions)
                emit versionString(v.first, v.second);
            for(const auto &tz : merged.timezones)
                emit timezone(tz.first, tz.second);
            for(const auto &u : merged.unregisters)
                emit unregisterContext(u[0], u[1], u[2]);
            for(int idx : merged.getLogInfo)
                appendToGetLogInfoList(idx);

            qDebug() << "Side effects replayed:" << merged.versions.size() << "version,"
                     << merged.timezones.size() << "timezone,"
                     << merged.unregisters.size() << "unregister,"
                     << merged.getLogInfo.size() << "getloginfo";
        }


        /* marker counts still have to be attributed, cheaply, over the hits */
        computeMarkerCountsFromIndex(filterList, &indexFilterList);

        emit(progress(100));
        qDebug() << "CFI:" << 100 << "%";
        qDebug() << "Create filter index: Finish (parallel)";
    }
    else
    {
    // Start reading messages
    for(ix=start;ix<end;ix++)
    {
        msg = QSharedPointer<QDltMsg>::create(); // create new instance to be filled by getMsg(), otherwise shared pointer would be empty or pointing to last message

        if(!dltFile->getMsg(ix, *msg))
            continue; // Skip broken messages

        indexerThread.processMessage(msg, ix);

        if((end-start)!=0)
            iPercent = ( (ix-start)*100 )/(end-start);
        if(iPercent>=progressCounter)
        {
            progressCounter += 1;
            emit progress(iPercent); // every 1%
            if((iPercent>0) && ((iPercent%10)==0))
                qDebug() << "CFI:" << iPercent << "%"; // every 10%
        }

        // stop if requested
        if(stopFlag)
        {
            /*if(useIndexerThread)
            {
                indexerThread.requestStop();
                indexerThread.wait();
            }*/

            return false;
        }
    }
    emit(progress(100));
    qDebug() << "CFI:" << 100 << "%";
    // destroy threads
    /*if(true == useIndexerThread)
    {
        indexerThread.requestStop();
        indexerThread.wait();
    }*/

    // update performance counter
    //msecsFilterCounter = time.elapsed();
    } // end of the serial path


    // use sorted values if sort by time enabled
    if(sortByTimeEnabled || sortByTimestampEnabled)
        indexFilterList = QVector<qint64>::fromList(indexFilterListSorted.values());

    // write filter index if enabled
    if(filterCacheEnabled)
    {
        saveFilterIndexCache(filterList, indexFilterList, filenames);
        qDebug() << "Saved filter index cache for files" << filenames;
    }

    /* remember this result so toggling filters off and on again is free */
    memoFilterKey = filenameFilterIndexCache(filterList, filenames);
    memoFilterIndex = indexFilterList;
    memoFilterValid = true;

    qDebug() << "Create filter index: Finish";

    return true;
}

QMap<QString, int> DltFileIndexer::getMarkerCounts() const
{
    QMutexLocker locker(&markerCountLock);
    return markerCounts;
}

void DltFileIndexer::addMarkerCount(const QString &filterName)
{
    if(filterName.isEmpty())
    {
        return;
    }

    QMutexLocker locker(&markerCountLock);
    markerCounts[filterName] = markerCounts.value(filterName, 0) + 1;
}

void DltFileIndexer::recomputeMarkerCounts(const QDltFilterList &filterList, const QVector<qint64> &indices)
{
    emit markerCountProgressMax(indices.size());
    emit markerCountProgressValue(0);

    resetMarkerCounts(filterList);
    computeMarkerCountsFromIndex(filterList, &indices);

    emit markerCountProgressValue(indices.size());
}

void DltFileIndexer::recomputeMarkerCountsForAllMessages(const QDltFilterList &filterList)
{
    const int total = dltFile ? dltFile->size() : 0;

    emit markerCountProgressMax(total);
    emit markerCountProgressValue(0);

    resetMarkerCounts(filterList);
    computeMarkerCountsFromIndex(filterList, nullptr);

    emit markerCountProgressValue(total);
}

void DltFileIndexer::resetMarkerCounts(const QDltFilterList &filterList)
{
    QMutexLocker locker(&markerCountLock);
    markerCounts.clear();

    for(int numfilter=0; numfilter<filterList.filters.size(); numfilter++)
    {
        QDltFilter *filter = filterList.filters[numfilter];
        if(filter != nullptr && filter->isMarker() && filter->enableFilter)
        {
            markerCounts.insert(filter->name, 0);
        }
    }
}

void DltFileIndexer::computeMarkerCountsFromIndex(const QDltFilterList &filterList, const QVector<qint64> *indices)
{
    // indices == nullptr means "every message in the file", expressed without
    // building an identity vector of one qint64 per message first.
    const int total = indices ? indices->size() : (dltFile ? dltFile->size() : 0);
    const int step = qMax(1, total / 200); // throttle UI updates

    for(int i = 0; i < total; ++i)
    {
        const qint64 rawIndex = indices ? (*indices)[i] : static_cast<qint64>(i);
        if(rawIndex < 0 || rawIndex > std::numeric_limits<int>::max())
        {
            continue;
        }

        QDltMsg msg;
        if(!dltFile->getMsg(static_cast<int>(rawIndex), msg))
        {
            continue;
        }

        const QDltFilter *markerFilter = filterList.matchMarkerFilter(msg);
        if(markerFilter != nullptr)
        {
            addMarkerCount(markerFilter->name);
        }

        if ((i % step) == 0 || i + 1 == total)
            emit markerCountProgressValue(i + 1);

        /* stop if requested */
        if(true == stopFlag)
        {
            qDebug().noquote() << "Request stopping marker count received" << __LINE__ << __FILE__;
            return;
        }
    }
}

bool DltFileIndexer::indexDefaultFilter()
{
    QSharedPointer<QDltMsg> msg;

    // start performance counter
    //QTime time;
    //time.start();

    // Initialise progress bar
    emit(progressText(QString("IF %1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(dltFile->size()));

    unsigned int modulo = dltFile->size()/100;
    if (modulo == 0) // avoid divison by zero
    {
         modulo = 1;
    }

    // clear all default filter cache index
    defaultFilter->clearFilterIndex();

    // get silent mode
    bool silentMode = !QDltOptManager::getInstance()->issilentMode();

    bool useDefaultFilterThread = defaultFilter->defaultFilterList.size() > 0;

    DltFileIndexerDefaultFilterThread defaultFilterThread
            (
                defaultFilter,
                pluginManager,
                silentMode
            );

    if(useDefaultFilterThread)
    {
        defaultFilterThread.start(QThread::NormalPriority);
    }

    /* run through the whole open file */
    for(int ix = 0; ix < dltFile->size(); ix++)
    {
        msg = QSharedPointer<QDltMsg>::create();
        /* Fill message from file */
        if(!dltFile->getMsg(ix, *msg))
        {
            /* Skip broken messages */
            continue;
        }

        if(useDefaultFilterThread)
            defaultFilterThread.enqueueMessage(msg, ix);
        else
            defaultFilterThread.processMessage(msg, ix);

        /* Update progress */
        if(ix % modulo == 0)
        {
            //qDebug() << "Index DefaultFilter" << ix;
            emit(progress(ix));
        }

        /* stop if requested */
        if(stopFlag)
        {
            if(useDefaultFilterThread)
            {
                defaultFilterThread.requestStop();
                defaultFilterThread.wait();
            }

            return false;
        }
    }

    if(useDefaultFilterThread)
    {
        defaultFilterThread.requestStop();
        defaultFilterThread.wait();
    }

    /* update plausibility checks of filter index cache, filename and filesize */
    for(int num=0; num < defaultFilter->defaultFilterIndex.size(); num++)
    {
        QDltFilterIndex *filterIndex;
        QDltFilterList *filterList;
        filterIndex = defaultFilter->defaultFilterIndex[num];
        filterList = defaultFilter->defaultFilterList[num];

        filterIndex->setDltFileName(dltFile->getFileName());
        filterIndex->setAllIndexSize(dltFile->size());

        // write filter index if enabled
        if(filterCacheEnabled)
            saveFilterIndexCache(*filterList, filterIndex->indexFilter, QStringList(dltFile->getFileName()));
    }

    // update performance counter
    //msecsDefaultFilterCounter = time.elapsed();
    //qDebug() << "Duration " << msecsDefaultFilterCounter;

    return true;
}


void DltFileIndexer::lock()
{
    indexLock.lock();
}

void DltFileIndexer::unlock()
{
    indexLock.unlock();
}

bool DltFileIndexer::tryLock()
{
    return indexLock.tryLock();
}

void DltFileIndexer::appendToGetLogInfoList(int value)
{
    getLogInfoList.append(value);
}

void DltFileIndexer::run()
{
    //qDebug() << "DltFileIndexer::run" << __FILE__ << __LINE__;
    // lock mutex while indexing
    QMutexLocker scopedLock(&indexLock);

    // initialise stop flag
    stopFlag = false;

    // clear performance counter
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;

    // get all active plugins
    activeViewerPlugins = pluginManager->getViewerPlugins();
    activeDecoderPlugins = pluginManager->getDecoderPlugins();

    // calculate runs
    if(mode == modeIndexAndFilter)
        maxRun = dltFile->getNumberOfFiles()+1;
    else
        maxRun = 1;

    currentRun = 1;

    // index
    if(mode == modeIndex || mode == modeIndexAndFilter)
    {
        for(int num=0;num < dltFile->getNumberOfFiles();num++)
        {
            if(!index(num))
            {
                qDebug() << "Error in indexer" << __FILE__ << __LINE__;
                return;
            }
           // qDebug() << "setDLTIndex" << num << __FILE__ << __LINE__;
            dltFile->setDltIndex(indexAllList,num);
            invalidateFilterMemo();  // the message index changed underneath it
            currentRun++;
        }
        emit(finishIndex());
    }
    else if(mode == modeNone)
    {
        // only update view
        emit(finishIndex());
    }

    // indexFilter
    if(mode == modeIndexAndFilter || mode == modeFilter)
    {
        QStringList filenames;
        for(int num=0;num<dltFile->getNumberOfFiles();num++)
            filenames.append(dltFile->getFileName(num));
        if((mode != modeNone) && !indexFilter(filenames))
        {
            // error
            return;
        }
        dltFile->enableFilter(filtersEnabled);
        dltFile->setIndexFilter(indexFilterList);
        emit(finishFilter());
    }

    // indexDefaultFilter
    if(mode == modeDefaultFilter)
    {
        if(false == indexDefaultFilter())
        {
            // error
            return;
        }
        emit(finishDefaultFilter());
    }

    //qDebug() << "Indexer run" << currentRun << "done" << __FILE__ <<  __LINE__;
    // print performance counter
    /*
    QTime time;
    time = QTime(0,0);time = time.addMSecs(msecsIndexCounter);
    qDebug() << "Duration Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";
    time = QTime(0,0);time = time.addMSecs(msecsFilterCounter);
    qDebug() << "Duration Filter Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";
    time = QTime(0,0);time = time.addMSecs(msecsDefaultFilterCounter);
    qDebug() << "Duration Default Filter Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";
    */
}

bool DltFileIndexer::getFilterIndexEnabled() const
{
    return filterIndexEnabled;
}

void DltFileIndexer::setFilterIndexEnabled(bool newFilterIndexEnabled)
{
    filterIndexEnabled = newFilterIndexEnabled;
}

qint64 DltFileIndexer::getFilterIndexStart() const
{
    return filterIndexStart;
}

void DltFileIndexer::setFilterIndexStart(qint64 newFilterIndexStart)
{
    filterIndexStart = newFilterIndexStart;
}

qint64 DltFileIndexer::getFilterIndexEnd() const
{
    return filterIndexEnd;
}

void DltFileIndexer::setFilterIndexEnd(qint64 newFilterIndexEnd)
{
    filterIndexEnd = newFilterIndexEnd;
}

void DltFileIndexer::stop()
{
    // stop the thread
    stopFlag = true;
    wait();
    //qDebug() << "Indexer stopped";
}

// load/safe index from/to file
bool DltFileIndexer::loadIndexCache(QString filename)
{
    QString filenameCache;

    // check if caching is enabled
    if(!filterCacheEnabled)
        return false;

    // get the filename for the cache file
    filenameCache = filenameIndexCache(filename);

    // load the cache file in a subdirectory index
    QFileInfo info(filename);
    QDir dir(info.dir().path()+"/index");
    if (!dir.exists())
        dir.mkpath(".");
    qDebug() << "Loading index cache" << info.dir().path() + "/index/" +filenameCache;
    if(!loadIndex(info.dir().path() + "/index/" +filenameCache,indexAllList))
    {
        // loading cache file failed
        return false;
    }

    return true;
}

bool DltFileIndexer::saveIndexCache(QString filename)
{
    QString filenameCache;

    // check if caching is enabled
    if(!filterCacheEnabled)
        return false;

    // get the filename for the cache file
    filenameCache = filenameIndexCache(filename);

    // save the cache file in a sudirectory index
    QFileInfo info(filename);
    QDir dir(info.dir().path()+"/index");
    if (!dir.exists())
        dir.mkpath(".");
    qDebug() << "Saving index cache" << info.dir().path() + "/index/" +filenameCache;
    const QString cachePath = info.dir().path() + "/index/" + filenameCache;
    if(!saveIndex(cachePath,indexAllList))
    {
        // saving cache file failed
        return false;
    }
    sessionCacheFiles.append(cachePath);

    return true;
}

QString DltFileIndexer::filenameIndexCache(QString filename)
{
    QString hashString;
    QByteArray hashByteArray;
    QByteArray md5;
    QString filenameCache;

    // create string to be hashed
    hashString = QFileInfo(filename).fileName();
    hashString += "_" + QString("%1").arg(dltFile->fileSize());

    // create byte array from hash string
    hashByteArray = hashString.toLatin1();

    // create MD5 from byte array
    md5 = QCryptographicHash::hash(hashByteArray, QCryptographicHash::Md5);

    // create filename
    filenameCache = QString(md5.toHex())+".dix";

    //qDebug() << filename << ">>" << filenameCache;

    return filenameCache;
}

// read/write index cache
bool DltFileIndexer::loadFilterIndexCache(QDltFilterList &filterList, QVector<qint64> &index, QStringList filenames)
{
    QString filenameCache;

    // check if caching is enabled
    if(!filterCacheEnabled)
        return false;

    // get the filename for the cache file
    filenameCache = filenameFilterIndexCache(filterList,filenames);

    // load the cache file
    QFileInfo info(filenames[0]);
    QDir dir(info.dir().path()+"/index");
    if (!dir.exists())
        dir.mkpath(".");
    if(loadIndex(info.dir().path() + "/index/" +filenameCache,index))
    {
        qDebug() << "loadIndex" << info.dir().path() + "/index/" +filenameCache << "success";
    }
    else
    {
        qDebug() << "loadIndex" << info.dir().path() + "/index/" +filenameCache << "failed";
        return false;
    }

    return true;
}

bool DltFileIndexer::saveFilterIndexCache(QDltFilterList &filterList, QVector<qint64> index, QStringList filenames)
{
    QString filename;

    // check if caching is enabled
    if(!filterCacheEnabled)
        return false;

    // get the filename for the cache file
    filename = filenameFilterIndexCache(filterList,filenames);

    // save the cache file
    QFileInfo info(filenames[0]);
    QDir dir(info.dir().path()+"/index");
    if (!dir.exists())
        dir.mkpath(".");
    const QString filterCachePath = info.dir().path() + "/index/" + filename;
    qDebug() << "Saving filter index cache" << filterCachePath;
    if(!saveIndex(filterCachePath,index))
    {
        // saving of cache file failed
        return false;
    }
    sessionCacheFiles.append(filterCachePath);

    return true;
}

QByteArray DltFileIndexer::md5ActiveDecoderPlugins()
{
    QByteArray md5;
    QString hashString = "Plugins";
    QByteArray hashByteArray;

    // walk through all active decoder plugins and generate String with all plugin names, version and loaded filename
    for(int num=0;num<activeDecoderPlugins.size();num++)
    {
        QDltPlugin *plugin = activeDecoderPlugins[num];

        hashString += plugin->name();
        hashString += plugin->pluginVersion();
        hashString += plugin->getFilename();
    }
    hashByteArray = hashString.toLatin1();

    // calculate hash value
    md5 = QCryptographicHash::hash(hashByteArray, QCryptographicHash::Md5);

    return md5;
}

QString DltFileIndexer::filenameFilterIndexCache(QDltFilterList &filterList,QStringList filenames)
{
    QString hashString;
    QByteArray hashByteArray;
    QByteArray md5;
    QByteArray md5FilterList;
    QString filename;

    // get filter list
    md5FilterList = filterList.createMD5();

    // create string to be hashed
    if(sortByTimeEnabled || sortByTimestampEnabled)
        filenames.sort();
    hashString = filenames.join(QString("_"));
    hashString += "_" + QString("%1").arg(dltFile->fileSize());

    // create byte array from hash string
    hashByteArray = hashString.toLatin1();

    // create MD5 from byte array
    md5 = QCryptographicHash::hash(hashByteArray, QCryptographicHash::Md5);

    // create filename
    filename = QString(md5.toHex()) + "_" + QString(md5FilterList.toHex());
    if(this->pluginsEnabled)
    {
        filename += "_" + QString(md5ActiveDecoderPlugins().toHex());
    }
    if(this->sortByTimeEnabled)
    {
        filename += "_S";
    }
    if(this->sortByTimestampEnabled)
    {
        filename += "_STS";
    }
    if(this->filterIndexEnabled)
    {
        filename += QString("_%1_%2").arg(this->filterIndexStart).arg(this->filterIndexEnd);
    }
    filename += ".dix";

    return filename;
}

bool DltFileIndexer::saveIndex(QString filename, const QVector<qint64> &index)
{
    quint32 version = DLT_FILE_INDEXER_FILE_VERSION;

    QFile file(filename);

    // open cache file
    if(!file.open(QFile::WriteOnly))
    {
        qWarning() << "DltFileIndexer: Failed to save index cache to" << filename << ":" << file.errorString();
        return false;
    }

    // write version
    if(file.write((char*)&version,sizeof(version)) != sizeof(version))
    {
        qWarning() << "DltFileIndexer: Failed to write index cache header to" << filename << ":" << file.errorString();
        return false;
    }

    // Write the index in bulk. Writing one qint64 per QFile::write() call costs
    // one write per message, which dominates the save for large files.
    const char *data = reinterpret_cast<const char*>(index.constData());
    const qint64 total = static_cast<qint64>(index.size()) * static_cast<qint64>(sizeof(qint64));
    qint64 written = 0;
    while(written < total)
    {
        const qint64 chunk = qMin<qint64>(DLT_FILE_INDEXER_IO_CHUNK, total - written);
        const qint64 result = file.write(data + written, chunk);
        if(result <= 0)
        {
            qWarning() << "DltFileIndexer: Failed to write index cache to" << filename << ":" << file.errorString();
            return false;
        }
        written += result;
    }

    // close cache file
    file.close();

    return true;
}

bool DltFileIndexer::loadIndex(QString filename, QVector<qint64> &index)
{
    quint32 version;
    int length;

    QFile file(filename);
    index.clear();

    // open cache file
    if(!file.open(QFile::ReadOnly))
    {
        qWarning() << "DltFileIndexer: Failed to load index cache from" << filename << ":" << file.errorString();
        return false;
    }

    qDebug() << "### Load index file";
    qDebug() << "Load index file " << filename;

    const qint64 fileSize = file.size();
    const qint64 payloadSize = fileSize - static_cast<qint64>(sizeof(version));
    if(payloadSize < 0 || (payloadSize % static_cast<qint64>(sizeof(qint64))) != 0)
    {
        qDebug() << "Loading index file " << filename << "failed: unexpected size" << fileSize;
        file.close();
        return false;
    }
    index.reserve(static_cast<qsizetype>(payloadSize / static_cast<qint64>(sizeof(qint64)))); // prevent memory issues through reallocation

    // read version
    length = file.read((char*)&version,sizeof(version));

    // compare version if valid
    if((length != sizeof(version)) || version != DLT_FILE_INDEXER_FILE_VERSION)
    {
        // wrong version number
        qDebug() << "Loading index file " << filename << "failed !";
        file.close();
        return false;
    }

    if(false == QDltOptManager::getInstance()->issilentMode())
    {
        emit(progressText(QString("LI %1/%2").arg(currentRun).arg(maxRun)));
        emit(progressMax(100));
    }
    else
    {
        qDebug().noquote() << "Load index: Start";
    }

    // Read the index in bulk. Reading one qint64 per QFile::read() call -- and
    // asking QFile::pos() for the progress bar on every one of them -- costs
    // two calls per message, which dominates the load for large files.
    unsigned int progressCounter = 1;
    emit(progress(0));

    QByteArray buffer;
    qint64 consumed = 0;
    while(consumed < payloadSize)
    {
        const qint64 want = qMin<qint64>(DLT_FILE_INDEXER_IO_CHUNK, payloadSize - consumed);
        buffer = file.read(want);
        if(buffer.size() != want)
        {
            qDebug() << "Loading index file " << filename << "failed: short read at" << consumed;
            file.close();
            return false;
        }

        const qint64 *values = reinterpret_cast<const qint64*>(buffer.constData());
        const qsizetype count = buffer.size() / static_cast<qsizetype>(sizeof(qint64));
        for(qsizetype num = 0; num < count; num++)
        {
            index.append(values[num]);
        }
        consumed += want;

        /* stop if requested */
        if(true == stopFlag)
        {
            qDebug().noquote() << "Request stopping index cache load received" << __LINE__ << __FILE__;
            file.close();
            return false;
        }

        const unsigned int percent = (fileSize > 0) ? static_cast<unsigned int>((consumed * 100) / fileSize) : 100;
        if(percent >= progressCounter)
        {
            progressCounter = percent + 1;
            emit(progress(percent));
            if((percent > 0) && ((percent % 10) == 0))
                qDebug() << "LI:" << percent << "%";
        }
    }

    // now that it is done we have to set the 100 %
    if(false == QDltOptManager::getInstance()->issilentMode())
    {
        emit(progress(100));
    }
    else
    {
        qDebug().noquote() << "Load index: Finish";
    }

    // close cache file
    file.close();

    return true;
}


qint64 DltFileIndexer::getfileerrors(void)
{
    return errors_in_file;
}

void DltFileIndexer::removeSessionCacheFiles()
{
    if(sessionCacheFiles.isEmpty())
        return;

    QSet<QString> dirs;
    int removed = 0;
    for(const QString &path : sessionCacheFiles)
    {
        QFileInfo info(path);
        dirs.insert(info.absolutePath());
        if(QFile::remove(path))
            removed++;
    }
    sessionCacheFiles.clear();

    /* take the index directory with it when nothing else is left in it, so the
       viewer does not leave an empty folder next to the user's logs */
    for(const QString &dirPath : dirs)
    {
        QDir dir(dirPath);
        if(dir.exists() && dir.dirName() == QStringLiteral("index") &&
           dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
        {
            dir.rmdir(dir.absolutePath());
        }
    }

    qDebug() << "Removed" << removed << "index cache files written this session";
}
