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
 * \file qdltfilereader.cpp
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#include "qdltfilereader.h"

#include <QtDebug>

QDltFileReader::QDltFileReader(const QDltFile &file)
{
    const int numFiles = file.getNumberOfFiles();
    for(int num = 0; num < numFiles; num++)
    {
        FileEntry *entry = new FileEntry();
        entry->name = file.getFileNameConst(num);
        entry->indexAll = file.getIndexAll(num);   // implicitly shared, cheap
        entry->fileSize = file.getFileSizeConst(num);
        totalMessages += entry->indexAll.size();
        files.append(entry);
    }
}

QDltFileReader::~QDltFileReader()
{
    for(FileEntry *entry : files)
    {
        if(entry->handle)
        {
            entry->handle->close();
            delete entry->handle;
        }
        delete entry;
    }
    files.clear();
}

QByteArray QDltFileReader::getMsg(int index)
{
    if(index < 0)
        return QByteArray();

    int num = 0;
    for(; num < files.size(); num++)
    {
        if(index < files[num]->indexAll.size())
            break;
        index -= files[num]->indexAll.size();
    }
    if(num >= files.size())
        return QByteArray();

    FileEntry *entry = files[num];

    if(!entry->handle)
    {
        entry->handle = new QFile(entry->name);
        if(!entry->handle->open(QIODevice::ReadOnly))
        {
            qWarning() << "QDltFileReader: cannot open" << entry->name << entry->handle->errorString();
            delete entry->handle;
            entry->handle = nullptr;
            return QByteArray();
        }
    }

    const qint64 pos = entry->indexAll[index];
    const qint64 msgLength = (index == entry->indexAll.size() - 1)
                                 ? (entry->fileSize - pos)
                                 : (entry->indexAll[index + 1] - pos);
    if(msgLength <= 0)
        return QByteArray();

    /* serve from this reader's own sequential window */
    if(entry->windowPos >= 0 &&
       pos >= entry->windowPos &&
       (pos + msgLength) <= (entry->windowPos + entry->window.size()))
    {
        return entry->window.mid(static_cast<qsizetype>(pos - entry->windowPos),
                                 static_cast<qsizetype>(msgLength));
    }

    if(!entry->handle->seek(pos))
        return QByteArray();

    if(msgLength >= DLT_FILE_READ_AHEAD_SIZE)
        return entry->handle->read(msgLength);

    entry->window = entry->handle->read(DLT_FILE_READ_AHEAD_SIZE);
    if(entry->window.size() < msgLength)
    {
        const QByteArray shortRead = entry->window;
        entry->window.clear();
        entry->windowPos = -1;
        return shortRead;
    }

    entry->windowPos = pos;
    return entry->window.mid(0, static_cast<qsizetype>(msgLength));
}
