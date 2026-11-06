#ifndef UPLOADER_H
#define UPLOADER_H

#include "protocol.h"

#include <QObject>

class Uploader : public QObject
{
    Q_OBJECT
public:
    Uploader();
    Uploader(QString strFilePath, qint64 offset = 0);

    QString m_strUploadFilePath;
    qint64 m_iUploadOffset;     //断点续传起始偏移量
    void start();
public slots:
    void uploadFile();
signals:
    void handlError(const QString& error);
    void uploadPDU(PDU* pdu);
    void finished();

signals:

};

#endif // UPLOADER_H
