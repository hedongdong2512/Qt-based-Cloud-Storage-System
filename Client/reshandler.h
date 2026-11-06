#ifndef RESHANDLER_H
#define RESHANDLER_H

#include "protocol.h"

#include <QFile>
#include <QString>



class ResHandler
{
public:
    ResHandler();
    PDU* m_pdu;
    QFile m_fDownloadFile;          //下载写入的本地文件
    qint64 m_iDownloadFileSize;     //待下载文件总大小
    qint64 m_iDownloadReceiveSize;  //已下载大小
    void regist();
    void login(QString strLoginName);
    void findUser();
    void onlineUser(QString strLoginName);
    void addFriend();
    void addFriendResend();
    void addFriendAgree();
    void flushFriend();
    void delFriend();
    void chat();
    void chatRespond();
    void mkdir();
    void flushFile();
    void delDir();
    void renameFile();
    void mvFile();
    void shareFile();
    void shareFileResend();
    void shareFileAgree();
    void uploadFileInit();
    void uploadFileData();
    void downloadFileInit();
    void downloadFileData();
    void offlineMsg();
};

#endif // RESHANDLER_H
