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
 * \file qdltfilereader.h
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#ifndef QDLT_FILE_READER_H
#define QDLT_FILE_READER_H

#include "export_rules.h"
#include "qdltfile.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QVector>

//! Read-only, thread-private view of an already indexed QDltFile.
/*!
  QDltFile serialises every message fetch on a single mutex and a single QFile
  handle, which is correct but means several threads reading the same log make
  no progress in parallel. A reader takes a snapshot of the message offsets --
  cheap, the vectors are implicitly shared -- and opens its own handles, so any
  number of them can walk disjoint ranges of the same file at once.

  The snapshot is only valid while the underlying index is: construct one after
  indexing has finished, and discard it when the index is rebuilt.
*/
class QDLT_EXPORT QDltFileReader
{
public:
    //! Snapshot the offsets and filenames of an indexed file.
    explicit QDltFileReader(const QDltFile &file);
    ~QDltFileReader();

    QDltFileReader(const QDltFileReader &) = delete;
    QDltFileReader &operator=(const QDltFileReader &) = delete;

    //! Total number of messages across all files in the snapshot.
    int size() const { return totalMessages; }

    //! Bytes of one message, by global index. Empty on any error.
    QByteArray getMsg(int index);

private:
    struct FileEntry
    {
        QString name;
        QVector<qint64> indexAll;
        qint64 fileSize = 0;
        QFile *handle = nullptr;      //!< opened lazily, owned
        QByteArray window;            //!< sequential read-ahead
        qint64 windowPos = -1;
    };

    QList<FileEntry*> files;
    int totalMessages = 0;
};

#endif // QDLT_FILE_READER_H
