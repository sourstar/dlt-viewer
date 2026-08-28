#ifndef DLTFILEUTILS_H
#define DLTFILEUTILS_H

#include <QDir>

class DltFileUtils : QObject
{
    Q_OBJECT
public:
    DltFileUtils();
    static QString createTempFile(QDir path,  bool silentmode);
    static QDir getTempPath(bool silentmode);

    //! Delete leftover zero-byte scratch files from a temp directory.
    static void pruneEmptyTempFiles(const QDir &path);
};

#endif // DLTFILEUTILS_H
