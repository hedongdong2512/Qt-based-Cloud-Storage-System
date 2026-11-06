#include "protocol.h"
#include "uploader.h"

#include <QFile>
#include <QThread>
#include <QDebug>

Uploader::Uploader()
{
    m_strUploadFilePath.clear();
    m_iUploadOffset = 0;
}

Uploader::Uploader(QString strFilePath, qint64 offset)
{
    m_strUploadFilePath = strFilePath;
    m_iUploadOffset = offset;
}

void Uploader::uploadFile()
{
    //创建文件对象并打开
    QFile file(m_strUploadFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit handlError("打开文件失败");
        emit finished();
        return;
    }

    //断点续传：跳过服务器已收到的部分，从偏移量处继续读取
    if (m_iUploadOffset > 0) {
        if (!file.seek(m_iUploadOffset)) {
            emit handlError("定位续传位置失败");
            emit finished();
            return;
        }
        qDebug() << "uploadFile resume from offset" << m_iUploadOffset;
    }
    //服务器已有部分正好是完整文件：补发一个空数据块，触发服务端结束应答
    if (m_iUploadOffset >= file.size()) {
        PDU* pdu = mkPDU(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_REQUEST, 0);
        pdu->uiMsgLen = 0;
        pdu->uiPDULen = sizeof(PDU);
        emit uploadPDU(pdu);
        file.close();
        emit finished();
        return;
    }

    //循环读取文件内容并发送
    while (true) {
        //构建pdu，每次发送40960
        PDU * pdu = mkPDU(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_REQUEST, 40960);
        int ret = file.read(pdu->caMsg, 40960);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            emit handlError("读取文件失败");
            break;
        }
        //最后一次读取长度可能不是4096，需要更新
        pdu->uiMsgLen = ret;
        pdu->uiPDULen = ret + sizeof(PDU);
        emit uploadPDU(pdu);
    }
    file.close();
    emit finished();
}

void Uploader::start()
{
    //创建线程对象
    QThread* thread = new QThread;
    //当前对象移交给线程
    this->moveToThread(thread);
    //线程执行后，当前对象执行uploadFile函数
    connect(thread, &QThread::started, this, &Uploader::uploadFile);
    //uploadFile运行完，线程退出
    connect(this, &Uploader::finished, thread, &QThread::quit);
    //线程结束，释放线程对象
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    //线程开始执行
    thread->start();
}
