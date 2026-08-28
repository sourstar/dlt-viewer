/**
 * @licence app begin@
 * Copyright (C) 2011-2012  BMW AG
 *
 * This file is part of COVESA Project Dlt Viewer.
 *
 * Contributions are licensed to the COVESA Alliance under one or more
 * Contribution License Agreements.
 *
 * \copyright
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0. If a  copy of the MPL was not distributed with
 * this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * \author Alexander Wenzel <alexander.aw.wenzel@bmw.de> 2011-2012
 *
 * \file qdlt.cpp
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#include <QFile>
#include <QtDebug>

#include <algorithm>

#include <QHash>

#include "qdltfile.h"

extern "C"
{
#include "dlt_common.h"
}

QDltFile::QDltFile()
{
    filterFlag = false;
    sortByTimeFlag = false;
    sortByTimestampFlag = false;

    cache.setMaxCost(1000);
    cacheEnable = true;
}

QDltFile::~QDltFile()
{
    clear();
}

void QDltFile::setCacheSize(qsizetype cost)
{
    if(cost==0)
    {
        cacheEnable = false;
        cache.setMaxCost(1);
    }
    else
    {
        cacheEnable = true;
        cache.setMaxCost(cost);
    }
}

void QDltFile::setDLTv2Support(bool _dltv2Support)
{
    dltv2Support = _dltv2Support;
}

bool QDltFile::getDLTv2Support() const
{
    return dltv2Support;
}

void QDltFile::clear()
{
    for(int num=0;num<files.size();num++)
    {
        if(files[num]->infile.isOpen()) {
             files[num]->infile.close();
        }
        delete(files[num]);
    }
    files.clear();

    cache.clear();

    indexFilter.clear();
    indexFilterBase.clear();

    manualMarkerIndices.clear();
}

int QDltFile::getNumberOfFiles() const
{
    return files.size();
}

void QDltFile::setDltIndex(QVector<qint64> &_indexAll, int num)
{
    if(num<0 || num>=files.size())
    {
        return;
    }

    /* getMsg() reads indexAll under mutexQDlt from the GUI thread while the
       indexer thread publishes a new one here; take the same lock. */
    QMutexLocker locker(&mutexQDlt);

    files[num]->indexAll = _indexAll;
    files[num]->invalidateReadBuffer();
}

int QDltFile::size() const
{
    int size=0;
    for(int num=0;num<files.size();num++)
    {
      if (nullptr!=files[num])
         size += files[num]->indexAll.size();
    }

    return size;
}

qint64 QDltFile::fileSize() const
{
    qint64 size=0;

    for(int num=0;num<files.size();num++)
    {
      if (nullptr!=files[num])
         size += files[num]->infile.size();
    }

    return size;
}

int QDltFile::sizeFilter() const
{
    if(filterFlag)
        return indexFilter.size();
    else
        return size();
}

int QDltFile::calculateHeaderSize(quint8 htyp)
{
    // Base DLT standard header size (HTYP (1) + MCNT (1) + LEN (2))
    int size = 4; 
    
    // Add standard header extra fields using DLT constants
    if (DLT_IS_HTYP_WEID(htyp)) size += DLT_SIZE_WEID; // ECU ID
    if (DLT_IS_HTYP_WSID(htyp)) size += DLT_SIZE_WSID; // Session ID  
    if (DLT_IS_HTYP_WTMS(htyp)) size += DLT_SIZE_WTMS; // Timestamp
    
    // Add extended header if UEH flag is set
    if (DLT_IS_HTYP_UEH(htyp)) {
        size += 10; // Extended header: MSIN (1) + NOAR (1) + APID (4) + CTID (4)
    }
    return size;
}

quint32 QDltFile::alignedStorageSize(quint32 size)
{
    constexpr int STORAGE_ALIGNMENT = 4;
    return ((size + STORAGE_ALIGNMENT - 1) / STORAGE_ALIGNMENT) * STORAGE_ALIGNMENT;
}

void QDltFile::setManualMarkerIndices(const QList<unsigned long int> &indices)
{
    QSet<qint64> newSet;
    newSet.reserve(indices.size());
    for(const auto &idx : indices)
    {
        newSet.insert(static_cast<qint64>(idx));
    }

    if(newSet == manualMarkerIndices)
    {
        return;
    }

    manualMarkerIndices = std::move(newSet);
    recomputeEffectiveIndexFilter();
}

bool QDltFile::open(QString _filename, bool append)
{
    qDebug() << "Open file" << _filename << "started";

    /* check if file is already opened */
    if(!append) {
        clear();
        // Reset size counters when opening new file
        totalStorageSize = 0;
        totalPayloadSize = 0;
        totalMessageSize = 0;
    }

    /* create new file item */
    QDltFileItem *item = new QDltFileItem();
    files.append(item);

    /* set new filename */
    item->infile.setFileName(_filename);

    /* open the log file read only */
    if(item->infile.open(QIODevice::ReadOnly)==false)
    {
        /* open file failed */
        qWarning() << "open of file" << _filename << "failed";
        return false;
    }

    return true;
}

quint64 QDltFile::getTotalStorageSize() { 
    if (totalStorageSize == 0 && size() > 0) {
        calculateTotalSizes();
    }
    return totalStorageSize; 
}

quint64 QDltFile::getTotalPayloadSize() { 
    if (totalPayloadSize == 0 && size() > 0) {
        calculateTotalSizes();
    }
    return totalPayloadSize; 
}

quint64 QDltFile::getTotalMessageSize() { 
    if (totalMessageSize == 0 && size() > 0) {
        calculateTotalSizes();
    }
    return totalMessageSize; 
}

void QDltFile::clearIndex()
{
    for(int num=0;num<files.size();num++)
    {
        files[num]->indexAll.clear();
        files[num]->invalidateReadBuffer();
    }
    
    // Reset size counters when clearing index
    totalStorageSize = 0;
    totalPayloadSize = 0;
    totalMessageSize = 0;
}

bool QDltFile::createIndex()
{
    bool ret = false;


    clearIndex();

    /* Example to use QtConcurrent to call updateIndex in separate Thread*/
    //QFuture<bool> f = QtConcurrent::run(this, &QDltFile::updateIndex);
    //f.waitForFinished();

    ret = updateIndex();

    //qDebug() << "Create index finished - " << size() << "messages found";

    // calculateTotalSizes() is deliberately not called here. It walks every
    // message with its own seek+read, i.e. a second full pass over the file,
    // and its only consumer is the "DLT File Size" dialog. The getters compute
    // it on demand instead.

    return ret;
}

bool QDltFile::updateIndex()
{
    QByteArray buf;
    qint64 pos = 0;
    quint16 last_message_length = 0;
    quint8 version=1;
    qint64 lengthOffset=2;
    qint64 storageLength=0;

    mutexQDlt.lock();

    for(int numFile=0;numFile<files.size();numFile++)
    {
        /* check if file is already opened */
        if(false == files[numFile]->infile.isOpen())
        {
            qDebug() << "updateMsg: Infile is not open" << files[numFile]->infile.fileName() << __FILE__ << "line" << __LINE__;
            mutexQDlt.unlock();
            return false;
        }


        /* start at last found position */
        if(files[numFile]->indexAll.size())
        {
            /* move behind last found position */
            const QVector<qint64>* const_indexAll = &(files[numFile]->indexAll);
            pos = (*const_indexAll)[files[numFile]->indexAll.size()-1];

            // first move to beginnng of last found message
            files[numFile]->infile.seek(pos);

            // read and get file storage length
            buf = files[numFile]->infile.read(14);
            if(((unsigned char)buf.at(3))==2)
            {
                storageLength = 14 + ((unsigned char)buf.at(13));
            }
            else
            {
                storageLength = 16;
            }

            // read and  get last message length
            files[numFile]->infile.seek(pos + storageLength);
            buf = files[numFile]->infile.read(7);
            version = (((unsigned char)buf.at(0))&0xe0)>>5;
            if(version==2)
            {
                lengthOffset = 5;
            }
            else
            {
                lengthOffset = 2;  // default
            }
            last_message_length = (unsigned char)buf.at(lengthOffset); // was 0
            last_message_length = (last_message_length<<8 | ((unsigned char)buf.at(lengthOffset+1))) + storageLength; // was 1

            // move just behind the next expected message
            pos += (last_message_length - 1);
            files[numFile]->infile.seek(pos);
        }
        else {
            /* the file was empty the last call */
            files[numFile]->infile.seek(0);
        }

        /* Align kbytes, 1MB read at a time */
        static const int READ_BUF_SZ = 1024 * 1024;

        /* walk through the whole file and find all DLT0x01 markers */
        /* store the found positions in the indexAll */
        char lastFound = 0;
        qint64 current_message_pos = 0;
        qint64 next_message_pos = 0;
        int counter_header = 0;
        quint16 message_length = 0;
        qint64 file_size = files[numFile]->infile.size();
        qint64 errors_in_file  = 0;

        quint8 progressNextCmdOutput=10;
        while(true)
        {
            if( (file_size>0) && ((pos*100/file_size)>=progressNextCmdOutput))
            {
                qDebug() << "CI:" << pos*100/file_size << "%";
                progressNextCmdOutput+=10;
            }

            /* read buffer from file */
            buf = files[numFile]->infile.read(READ_BUF_SZ);
            if(buf.isEmpty())
                break; // EOF

            /* Use primitive buffer for faster access */
            int cbuf_sz = buf.size();
            const char *cbuf = buf.constData();

            /* find marker in buffer */
            for(int num=0;num<cbuf_sz;num++) {
                // search length of DLT message
                if(counter_header>0)
                {
                    counter_header++;
                    if(storageLength==13 && counter_header==13)
                    {
                        storageLength += ((unsigned char)cbuf[num]) + 1;
                    }
                    else if (counter_header==storageLength)
                    {
                        // Read DLT protocol version
                        version = (((unsigned char)cbuf[num])&0xe0)>>5;
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
                    else if (counter_header==(storageLength+lengthOffset)) // was 16
                    {
                        // Read low byte of message length
                        message_length = (unsigned char)cbuf[num];
                    }
                    else if (counter_header==(storageLength+1+lengthOffset)) // was 17
                    {
                        // Read high byte of message length
                        counter_header = 0;
                        message_length = (message_length<<8 | ((unsigned char)cbuf[num])) + storageLength;
                        next_message_pos = current_message_pos + message_length;
                        if(next_message_pos==file_size)
                        {
                            // last message found in file
                            files[numFile]->indexAll.append(current_message_pos);
                            break;
                        }
                        // speed up move directly to next message, if inside current buffer
                        if((message_length > storageLength+2+lengthOffset)) // was 20
                        {
                            if((num+message_length-(storageLength+2+lengthOffset)<cbuf_sz))  // was 20
                            {
                                num+=message_length-(storageLength+2+lengthOffset);  // was 20
                            }
                        }
                    }
                }
                else if(cbuf[num] == 'D')
                {
                    lastFound = 'D';
                }
                else if(lastFound == 'D' && cbuf[num] == 'L')
                {
                    lastFound = 'L';
                }
                else if(lastFound == 'L' && cbuf[num] == 'T')
                {
                    lastFound = 'T';
                }
                else if(lastFound == 'T' && (cbuf[num] == 0x01 || cbuf[num] == 0x02))
                {
                    if(next_message_pos == 0)
                    {
                        // first message detected or first message after error
                        current_message_pos = pos+num-3;
                        counter_header = 3;
                        if(cbuf[num] == 0x01)
                            storageLength = 16;
                        else
                            storageLength = 13;
                        if(current_message_pos!=0)
                        {
                            // first messages not at beginning or error occurred before
                            errors_in_file++;
                        }
                        // speed up move directly to message length, if inside current buffer
                        if(num+9<cbuf_sz)
                        {
                            num+=9;
                            counter_header+=9;
                        }
                    }
                    else if( next_message_pos == (pos+num-3) )
                    {
                        // Add message only when it is in the correct position in relationship to the last message
                        files[numFile]->indexAll.append(current_message_pos);
                        current_message_pos = pos+num-3;
                        counter_header = 3;
                        if(cbuf[num] == 0x01)
                            storageLength = 16;
                        else
                            storageLength = 13;
                        // speed up move directly to message length, if inside current buffer
                        if(num+9<cbuf_sz)
                        {
                            num+=9;
                            counter_header+=9;
                        }
                    }
                    else if(next_message_pos > (pos+num-3))
                    {
                        // Header detected before end of message
                    }
                    else
                    {
                        // Header detected after end of message
                        // start search for new message back after last header found
                        files[numFile]->infile.seek(current_message_pos+4);
                        pos = current_message_pos+4;
                        buf = files[numFile]->infile.read(READ_BUF_SZ);
                        cbuf_sz = buf.size();
                        cbuf = buf.constData();
                        num=0;
                        next_message_pos = 0;
                    }
                    lastFound = 0;
                }
                else
                {
                    lastFound = 0;
                }
            }
            pos += cbuf_sz;
        }
    }

    for(int numFile=0;numFile<files.size();numFile++)
    {
        // the file may have grown while it was being scanned
        files[numFile]->invalidateReadBuffer();
    }

    mutexQDlt.unlock();

    /* success */
    return true;
}


bool QDltFile::createIndexFilter()
{
    /* clear old index */
    indexFilterBase.clear();

    return updateIndexFilter();
}

bool QDltFile::updateIndexFilter()
{
    QDltMsg msg;
    QByteArray buf;
    int index;

    /* update index filter by starting from last found index in list */

    /* get lattest found index in filter list */
    if(indexFilterBase.size()>0) {
        index = indexFilterBase[indexFilterBase.size()-1] + 1;
    }
    else {
        index = 0;
    }

    quint8 progressNextCmdOutput=10;
    for(int num=index;num<size();num++) {

        if( (size()>0) && ((num*100/size())>=progressNextCmdOutput))
        {
            qDebug() << "CFI:" << num*100/size() << "%";
            progressNextCmdOutput+=10;
        }

        buf = getMsg(num);
        if(!buf.isEmpty()) {
            msg.setMsg(buf,true,dltv2Support);
            msg.setIndex(num);
            if(checkFilter(msg)) {
                indexFilterBase.append(num);
            }
        }

    }

    recomputeEffectiveIndexFilter();

    return true;
}

bool QDltFile::checkFilter(QDltMsg &msg)
{
    if(!filterFlag)
    {
        return true;
    }

    return filterList.checkFilter(msg);
}

QDltFilterList QDltFile::getFilterList() const
{
    return filterList;
}

void QDltFile::setFilterList(QDltFilterList &_filterList)
{
    filterList = _filterList;
}

void QDltFile::clearFilterIndex()
{
    /* clear old index */
    indexFilterBase.clear();
    recomputeEffectiveIndexFilter();

}

void QDltFile::addFilterIndex (int index)
{
    indexFilterBase.append(index);

    // Recomputing the whole merged view for every appended message is O(n) per
    // call and therefore O(n^2) over a live session with manual markers set.
    // The merge only has to be redone when the output order depends on message
    // content, i.e. when sorting by time or timestamp is active.
    if(!manualMarkerIndices.isEmpty() && (sortByTimeFlag || sortByTimestampFlag))
    {
        recomputeEffectiveIndexFilter();
        return;
    }

    if(manualMarkerIndices.contains(index))
    {
        // Already present in indexFilter: it was merged in as a manual marker
        // before it became part of the filter result.
        return;
    }

    indexFilter.append(index);
}

#ifdef USECOLOR
    QColor QDltFile::checkMarker(const QDltMsg &msg)
    {
        if(!filterFlag)
        {
            return QColor();
        }

        return filterList.checkMarker(msg);
    }

#else
 QString QDltFile::checkMarker(const QDltMsg &msg)
 {
     if(!filterFlag)
     {
         return QString(""); // invalid colour
     }

     return filterList.checkMarker(msg);
 }
#endif


QString QDltFile::getFileName(int num)
{
    if(num<0 || num>=files.size())
        return QString();

    return files[num]->infile.fileName();
}

int QDltFile::getFileMsgNumber(int num) const
{
    if(num<0 || num>=files.size())
        return -1;

    return files[num]->indexAll.size();
}

void QDltFile::close()
{
    /* close file */
    clear();
    // Reset size counters when closing
    totalPayloadSize = 0;
    totalMessageSize = 0;
    totalStorageSize = 0;
}

QByteArray QDltFile::getMsg(int index) const
{
    QByteArray buf;
    int num = 0;

    /* check if index is in range */
    if( index < 0 )
    {
        qDebug() << "getMsg: Index is out of range" << __FILE__ << "line" << __LINE__;
        return QByteArray();
    }

    for( num=0; num < files.size(); num++ )
    {
        if(index < files[num]->indexAll.size())
            break;
        else
            index -= files[num]->indexAll.size();
    }

    if(num >= files.size())
    {
        qDebug() << "getMsg: Index is out of range in" << __FILE__ << "line" << __LINE__;
        return QByteArray();
    }

    /* check if file is already opened */
    if(false == files[num]->infile.isOpen())
    {
        qDebug() << "getMsg: Infile is not open" << files[num]->infile.fileName() << __FILE__ << "line" << __LINE__;
        return QByteArray();
    }

    QMutexLocker locker(&mutexQDlt);

    QDltFileItem* file = files[num];
    const qint64 positionForIndex = file->indexAll[index];

    /* length of this message: up to the next one, or up to end of file */
    qint64 msgLength;
    if(index == (file->indexAll.size()-1))
        msgLength = file->infile.size() - positionForIndex;
    else
        msgLength = file->indexAll[index+1] - positionForIndex;

    if(msgLength <= 0)
    {
        qDebug() << "Negativ index " << msgLength << index << "in" << file->infile.fileName() << __LINE__ << "of" << __FILE__;
        return QByteArray();
    }

    /* Sequential read-ahead.
     *
     * Every full-file pass -- filter indexing, export, search, marker counting
     * -- walks the messages in order, so one block read serves thousands of
     * them. The window is per thread: parallel Find-All runs several threads
     * over disjoint regions, and a single shared window would just be evicted
     * back and forth. It is keyed by file and by a generation counter, so a
     * rebuilt index or a grown file invalidates it without needing to reach
     * into other threads. */
    struct ReadAhead
    {
        const QDltFileItem *owner = nullptr;
        quint64 generation = 0;
        qint64 pos = -1;
        QByteArray data;
    };
    static thread_local ReadAhead ra;

    const bool windowValid = (ra.owner == file) && (ra.generation == file->generation) && (ra.pos >= 0);
    if(windowValid &&
       positionForIndex >= ra.pos &&
       (positionForIndex + msgLength) <= (ra.pos + ra.data.size()))
    {
        return ra.data.mid(static_cast<qsizetype>(positionForIndex - ra.pos),
                           static_cast<qsizetype>(msgLength));
    }

    if( false == file->infile.seek(positionForIndex) )
    {
        qDebug() << "Seek error on " << positionForIndex << file->infile.fileName() << __FILE__ << __LINE__;
        return QByteArray();
    }

    if(msgLength >= DLT_FILE_READ_AHEAD_SIZE)
    {
        /* larger than the window: read it straight through */
        return file->infile.read(msgLength);
    }

    ra.data = file->infile.read(DLT_FILE_READ_AHEAD_SIZE);
    if(ra.data.size() < msgLength)
    {
        /* short read: hand back what the file actually has */
        buf = ra.data;
        ra.owner = nullptr;
        ra.pos = -1;
        ra.data.clear();
        return buf;
    }

    ra.owner = file;
    ra.generation = file->generation;
    ra.pos = positionForIndex;
    return ra.data.mid(0, static_cast<qsizetype>(msgLength));
}

bool QDltFile::getMsg(int index,QDltMsg &msg)
{
    bool result;
    QDltMsg *cacheMsg;

    // check if msg is already in cache
    if(cacheEnable)
    {
        mutexQDlt.lock();
        cacheMsg = cache[index];
        if(cacheMsg)
        {
            // load from cache
            msg = *cacheMsg;
            mutexQDlt.unlock();
            return true;
        }
        mutexQDlt.unlock();
    }

    // load message from DLT file
    QByteArray data = getMsg(index);
    if(data.isEmpty())
        return false;
    result = msg.setMsg(data,true,dltv2Support);
    msg.setIndex(index);

    // store msg in cache
    if(cacheEnable && result)
    {
        cacheMsg = new QDltMsg();
        *cacheMsg = msg;
        mutexQDlt.lock();
        if(!cache.insert(index,cacheMsg))
        {
            // object deleted already by insert function
            // delete cacheMsg;
        }
        mutexQDlt.unlock();
    }

    return result;
}

QByteArray QDltFile::getMsgFilter(int index) const
{
    if(filterFlag)
    {
        int msgIndex;
        {
            QMutexLocker locker(&mutexQDlt);
            if(index<0 || index>=indexFilter.size())
            {
                qDebug() << "getMsg: Index is out of range" << __FILE__ << "line" << __LINE__;
                return QByteArray();
            }
            msgIndex = indexFilter[index];
        }
        return getMsg(msgIndex);
    }
    else
    {
        /* check if index is in range */
        if(index<0 || index>=size())
        {
         qDebug() << "getMsg: Index" << index << "is out of range" << size() << __FILE__ << "line" << __LINE__;
         /* return empty data buffer */
         return QByteArray();
        }
        return getMsg(index);
    }
}

int QDltFile::getMsgFilterPos(int index) const
{
    if(filterFlag)
    {
        /* check if index is in range */
        if(index<0 || index>=indexFilter.size())
        {
        //qDebug() << "getMsg: Index is out of range" << __FILE__ << "line" << __LINE__;
        qDebug() << "getMsg: Index" << index << "is out of range" << indexFilter.size() << __FILE__ << "line" << __LINE__;
        /* return invalid */
        return -1;
        }
        return indexFilter[index];
    }
    else {
        /* check if index is in range */
        if(index<0 || index>=size())
        {
         qDebug() << "getMsg: Index is out of range" << __FILE__ << "line" << __LINE__;
        /* return invalid */
        return -1;
        }
        return index;
    }
}

void QDltFile::clearFilter()
{
    filterList.clearFilter();
}

void QDltFile::addFilter(QDltFilter *_filter)
{
    filterList.addFilter(_filter);
}

void QDltFile::updateSortedFilter()
{
    filterList.updateSortedFilter();
}

bool QDltFile::isFilter() const
{
    return filterFlag;
}

void QDltFile::enableFilter(bool state)
{
    filterFlag = state;
}

void QDltFile::enableSortByTime(bool state)
{
    sortByTimeFlag = state;
}

void QDltFile::enableSortByTimestamp(bool state)
{
    sortByTimestampFlag = state;
}

QVector<qint64> QDltFile::getIndexFilter() const
{
    return indexFilter;
}

void QDltFile::setIndexFilter(QVector<qint64> _indexFilter)
{
    indexFilterBase = std::move(_indexFilter);
    recomputeEffectiveIndexFilter();
}

QVector<qint64> QDltFile::mergeIndexFilterBaseWithMarkers(const QSet<qint64> &markerSet) const
{
    // indexFilterBase comes from the indexer and may be sorted by time or timestamp.
    // Do NOT assume it is sorted by message index. Preserve the base ordering and insert
    // missing markers according to the current sort mode.

    if(markerSet.isEmpty())
        return indexFilterBase;

    const qint64 maxIdx = static_cast<qint64>(size());

    // Fast membership check: which indices are already present in the base filter output.
    QSet<qint64> present;
    present.reserve(indexFilterBase.size());
    for(const auto &idx : indexFilterBase)
        present.insert(idx);

    QVector<qint64> missingMarkers;
    missingMarkers.reserve(markerSet.size());
    for(const auto &manualIdx : markerSet)
    {
        if(manualIdx < 0 || manualIdx >= maxIdx)
            continue;
        if(present.contains(manualIdx))
            continue;
        missingMarkers.append(manualIdx);
    }

    if(missingMarkers.isEmpty())
        return indexFilterBase;

    struct SortKey
    {
        qint64 primary;
        quint32 secondary;
        qint64 index;
    };

    QHash<qint64, SortKey> keyCache;
    keyCache.reserve(indexFilterBase.size() + missingMarkers.size());

    const bool sortByTime = sortByTimeFlag;
    const bool sortByTimestamp = sortByTimestampFlag;

    const auto keyForIndex = [&](qint64 idx) -> SortKey {
        const auto it = keyCache.constFind(idx);
        if(it != keyCache.constEnd())
            return it.value();

        SortKey key{idx, 0u, idx};

        if(idx >= 0 && idx < maxIdx && (sortByTime || sortByTimestamp))
        {
            const QByteArray data = getMsg(static_cast<int>(idx));
            if(!data.isEmpty())
            {
                QDltMsg msg;
                if(msg.setMsg(data, true, dltv2Support))
                {
                    if(sortByTime)
                    {
                        key.primary = static_cast<qint64>(msg.getTime());
                        key.secondary = msg.getMicroseconds();
                    }
                    else
                    {
                        key.primary = static_cast<qint64>(msg.getTimestamp());
                        key.secondary = 0u;
                    }
                    key.index = idx;
                }
            }
        }

        keyCache.insert(idx, key);
        return key;
    };

    const auto lessByCurrentSort = [&](qint64 lhs, qint64 rhs) -> bool {
        const SortKey a = keyForIndex(lhs);
        const SortKey b = keyForIndex(rhs);
        if(a.primary != b.primary)
            return a.primary < b.primary;
        if(a.secondary != b.secondary)
            return a.secondary < b.secondary;
        return a.index < b.index;
    };

    // Sort missing markers once, then merge in a single linear pass.
    std::sort(missingMarkers.begin(), missingMarkers.end(), lessByCurrentSort);

    QVector<qint64> merged;
    merged.reserve(indexFilterBase.size() + missingMarkers.size());

    auto baseIt = indexFilterBase.constBegin();
    auto baseEnd = indexFilterBase.constEnd();
    auto markerIt = missingMarkers.constBegin();
    auto markerEnd = missingMarkers.constEnd();

    while(baseIt != baseEnd && markerIt != markerEnd)
    {
        if(lessByCurrentSort(*markerIt, *baseIt))
            merged.append(*markerIt++);
        else
            merged.append(*baseIt++);
    }

    while(baseIt != baseEnd)
        merged.append(*baseIt++);
    while(markerIt != markerEnd)
        merged.append(*markerIt++);

    return merged;
}

void QDltFile::recomputeEffectiveIndexFilter()
{
    indexFilter = mergeIndexFilterBaseWithMarkers(manualMarkerIndices);
}
bool QDltFile::applyRegExString(QDltMsg &msg,QString &text)
{

    return filterList.applyRegExString(msg,text);
}

bool QDltFile::applyRegExStringMsg(QDltMsg &msg) const
{    
    return filterList.applyRegExStringMsg(msg);
}

void QDltFile::calculateTotalSizes()
{
    // Structure to hold per-message size info
    struct QDltMsgSizeInfo {
        quint32 storageSize;
        quint32 messageSize;
        quint32 payloadSize;
    };

    // Get total number of indexed messages
    const int totalMessages = size();
    if (totalMessages == 0) {
        totalStorageSize = 0;
        totalMessageSize = 0;
        totalPayloadSize = 0;
        return;
    }

    // Create cache for per-message size info
    QVector<QDltMsgSizeInfo> msgSizeCache(totalMessages);

    // Progress reporting interval for large files
    const int progressInterval = qMax(1, qMin(totalMessages / 20, 10000));
    for (int msgIndex = 0; msgIndex < totalMessages; msgIndex++) {
        // Print progress for large files
        if (totalMessages > 10000 && (msgIndex % progressInterval) == 0) {
            int percentage = (msgIndex * 100) / totalMessages;
            qDebug() << "Caching sizes:" << percentage << "% (" << msgIndex << "/" << totalMessages << ")";
        }

        QByteArray msgData = getMsg(msgIndex);
        QDltMsgSizeInfo info = {0, 0, 0};
        if (msgData.isEmpty() || msgData.size() < 20) {
            msgSizeCache[msgIndex] = info;
            continue;
        }

        // Parse storage header
        const char *data = msgData.constData();
        const int msgDataSize = msgData.size();
        const quint8 storageHeaderVersion = static_cast<quint8>(data[3]);
        int storageHeaderSize = 16;
        if (storageHeaderVersion == 2) {
            // DLTv2: storage header size depends on ECU ID length
            if (msgDataSize < 14) {
                msgSizeCache[msgIndex] = info;
                continue;
            }
            const quint8 ecuIdLength = static_cast<quint8>(data[13]);
            storageHeaderSize = 14 + ecuIdLength;
        }

        // Validate enough data for DLT header
        if (msgDataSize < storageHeaderSize + 4) {
            msgSizeCache[msgIndex] = info;
            continue;
        }

        // Parse DLT protocol header
        const char* dltHeaderData = data + storageHeaderSize;
        const quint8 htyp = static_cast<quint8>(dltHeaderData[0]);
        const quint16 dltMessageLength = (static_cast<quint8>(dltHeaderData[2]) << 8) |
                                         static_cast<quint8>(dltHeaderData[3]);

        // Validate message length
        if (dltMessageLength == 0 ||
            dltMessageLength > (msgDataSize - storageHeaderSize)) {
            msgSizeCache[msgIndex] = info;
            continue;
        }

        // Calculate header and payload sizes
        const int headerSize = calculateHeaderSize(htyp);
        const int payloadSize = dltMessageLength - headerSize;
        if (payloadSize < 0) {
            msgSizeCache[msgIndex] = info;
            continue;
        }

        info.storageSize = alignedStorageSize(msgDataSize);
        info.messageSize = dltMessageLength;
        info.payloadSize = payloadSize;
        msgSizeCache[msgIndex] = info;
    }
    if (totalMessages > 10000) {
        qDebug() << "Size caching completed: processed" << totalMessages << "messages";
    }

    // Accumulate totals from cache
    totalStorageSize = 0;
    totalMessageSize = 0;
    totalPayloadSize = 0;
    for (int i = 0; i < msgSizeCache.size(); ++i) {
        totalStorageSize += msgSizeCache[i].storageSize;
        totalMessageSize += msgSizeCache[i].messageSize;
        totalPayloadSize += msgSizeCache[i].payloadSize;
    }
}
