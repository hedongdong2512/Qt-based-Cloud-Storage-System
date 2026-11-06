#include "msghandler.h"
#include "mytcpserver.h"
#include "operatedb.h"
#include "server.h"
#include "redisclient.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

MsgHandler::MsgHandler()
{

}

PDU *MsgHandler::regist()
{
    //从caData中取出用户名和密码
    char caName[32] = {'\0'};
    char caPwd[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    memcpy(caPwd, m_pdu->caData+32, 32);
    qDebug() << "regist caName" << caName
             << "caPwd" << caPwd;

    bool ret = OperateDB::getInstance().handleRegist(caName, caPwd);
    qDebug() << "regist ret" << ret;
    if (ret) {
        QDir dir;
        dir.mkdir(QString("%1/%2").arg(Server::getInstance().m_strRootPath).arg(caName));
    }

    PDU* respdu = mkPDU(ENUM_MSG_TYPE_REGIST_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::login(QString &strLoginName)
{
    //从caData中取出用户名和密码
    char caName[32] = {'\0'};
    char caPwd[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    memcpy(caPwd, m_pdu->caData+32, 32);
    qDebug() << "login caName" << caName
             << "caPwd" << caPwd;

    bool ret = OperateDB::getInstance().handleLogin(caName, caPwd);
    qDebug() << "handleLogin ret" << ret;
    if (ret) {
        strLoginName = caName;
    }

    PDU* respdu = mkPDU(ENUM_MSG_TYPE_LOGIN_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::findUser()
{
    char caName[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    int ret = OperateDB::getInstance().handleFindUser(caName);
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_FIND_USER_RESPOND);
    memcpy(respdu->caData, caName, 32);
    memcpy(respdu->caData+32, &ret, sizeof(int));
    return respdu;
}

PDU *MsgHandler::onlineUser()
{
    QStringList result = OperateDB::getInstance().handleOnlineUser();

    PDU* respdu = mkPDU(ENUM_MSG_TYPE_ONLINE_USER_RESPOND, result.size()*32);

    qDebug() << "result.size()" << result.size();
    for (int i=0; i<result.size(); i++) {
        qDebug() << "result.at(i)" << result.at(i);
        memcpy(respdu->caMsg+i*32, result.at(i).toStdString().c_str(), 32);
    }
    return respdu;
}

PDU *MsgHandler::addFriend()
{
    char curName[32] = {'\0'};
    char tarName[32] = {'\0'};
    memcpy(curName, m_pdu->caData, 32);
    memcpy(tarName, m_pdu->caData+32, 32);

    int ret = OperateDB::getInstance().handleAddFriend(curName, tarName);
    if (ret == 1) {
        MyTcpServer::getInstance().resend(tarName, m_pdu);
    }
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_ADD_FRIEND_RESPOND);
    memcpy(respdu->caData, &ret, sizeof (int));
    return respdu;
}

PDU *MsgHandler::addFriendAgree()
{
    char curName[32] = {'\0'};
    char tarName[32] = {'\0'};
    memcpy(curName, m_pdu->caData, 32);
    memcpy(tarName, m_pdu->caData+32, 32);

    bool ret = OperateDB::getInstance().handleAddFriendAgree(curName, tarName);
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_ADD_FRIEND_AGREE_RESPOND);
    memcpy(respdu->caData, &ret, sizeof (bool));
    MyTcpServer::getInstance().resend(curName, respdu);
    return respdu;
}

PDU *MsgHandler::flushFriend()
{
    char curName[32] = {'\0'};
    memcpy(curName, m_pdu->caData, 32);
    QStringList res = OperateDB::getInstance().handleOnlineFriend(curName);
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_FLUSH_FRIEND_RESPOND, res.size()*32);
    for (int i = 0; i < res.size(); i++) {
        memcpy(respdu->caMsg+i*32, res.at(i).toStdString().c_str(), 32);
    }
    return respdu;
}

PDU *MsgHandler::delFriend()
{
    char curName[32] = {'\0'};
    char tarName[32] = {'\0'};
    memcpy(curName, m_pdu->caData, 32);
    memcpy(tarName, m_pdu->caData+32, 32);
    bool ret = OperateDB::getInstance().handleDelFriend(curName, tarName);
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_DEL_FRIEND_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    if (ret) {
        MyTcpServer::getInstance().resend(tarName, respdu);
    }
    return respdu;
}

PDU *MsgHandler::chat()
{
    char curName[32] = {'\0'};
    char tarName[32] = {'\0'};
    memcpy(curName, m_pdu->caData, 32);
    memcpy(tarName, m_pdu->caData+32, 32);
    //目标在线：直接转发
    if (MyTcpServer::getInstance().resend(tarName, m_pdu)) {
        return NULL;
    }
    //目标离线：写入Redis离线消息队列，待对方上线时拉取
    //消息格式："发送者:内容"，Redis key 为 offline:<目标用户名>（List 结构）
    QString strKey = QString("offline:%1").arg(tarName);
    QString strValue = QString("%1:%2").arg(curName).arg(QString::fromUtf8(m_pdu->caMsg));
    bool ret = RedisClient::getInstance().rpush(strKey, strValue);
    qDebug() << "chat target offline, save to redis" << strKey << strValue << "ret" << ret;
    //告知发送方：对方不在线，消息已存为离线消息
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_CHAT_RESPOND, 128);
    QString strTip = ret
            ? QString("对方不在线，消息已存为离线消息，对方上线后会收到")
            : QString("对方不在线，且离线消息保存失败，请稍后重试");
    QByteArray baTip = strTip.toUtf8();
    memcpy(respdu->caMsg, baTip.constData(), baTip.size());
    return respdu;
}

PDU *MsgHandler::mkdir()
{
    char dirName[32] = {'\0'};
    memcpy(dirName, m_pdu->caData, 32);
    QDir dir;
    bool ret = dir.mkdir(QString("%1/%2").arg(m_pdu->caMsg).arg(dirName));
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_MKDIR_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::flushFile()
{
    //获取当前路径下的所有文件信息
    QDir dir(m_pdu->caMsg);
    QFileInfoList fileList = dir.entryInfoList();

    //构建响应pdu，caMsg放当前路径下的所有文件信息，长度就是文件信息结构体大小乘以文件个数
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_FLUSH_FILE_RESPOND, sizeof(FileInfo) * (fileList.size() - 2));

    //遍历文件信息，提取文件名和文件类型，构建结构体放入caMsg
    QString strFileName;
    FileInfo* pFileInfo;
    qDebug() << "fileList.size()" << fileList.size();
    for (int i = 0, j = 0; i < fileList.size(); i++) {
        strFileName = fileList.at(i).fileName();
        if (strFileName == QString(".") || strFileName == QString("..")) {
            continue;
        }
        pFileInfo = (FileInfo*)respdu->caMsg+j++;

        memcpy(pFileInfo->caName, strFileName.toStdString().c_str(), 32);
        if (fileList.at(i).isDir()) {
            pFileInfo->iFileType = 0;
        } else {
            pFileInfo->iFileType = 1;
        }
        qDebug() << "pFileInfo->caName" << pFileInfo->caName<< "pFileInfo->iFileType" << pFileInfo->iFileType;
    }
    return respdu;
}

PDU *MsgHandler::delDir()
{
    QFileInfo fileInfo(m_pdu->caMsg);
    bool ret = false;
    if (fileInfo.isDir()) {
        QDir dir(m_pdu->caMsg);
        ret = dir.removeRecursively();
    }
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_DEL_DIR_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::renameFile()
{
    char caOldName[32] = {'\0'};
    char caNewName[32] = {'\0'};
    memcpy(caOldName, m_pdu->caData, 32);
    memcpy(caNewName, m_pdu->caData+32, 32);

    QString strOldPath = QString("%1/%2").arg(m_pdu->caMsg).arg(caOldName);
    QString strNewPath = QString("%1/%2").arg(m_pdu->caMsg).arg(caNewName);

    PDU* respdu = mkPDU(ENUM_MSG_TYPE_RENAME_FILE_RESPOND);
    QDir dir;
    bool ret = dir.rename(strOldPath, strNewPath);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::mvFile()
{
    int srcLen = 0;
    int tarLen = 0;
    memcpy(&srcLen, m_pdu->caData, sizeof(int));
    memcpy(&tarLen, m_pdu->caData+32, sizeof(int));

    char* pSrcPath = new char[srcLen+1];
    char* pTarPath = new char[tarLen+1];
    memset(pSrcPath, '\0', srcLen+1);
    memset(pTarPath, '\0', tarLen+1);
    memcpy(pSrcPath, m_pdu->caMsg, srcLen);
    memcpy(pTarPath, m_pdu->caMsg+srcLen, tarLen);

    PDU* respdu = mkPDU(ENUM_MSG_TYPE_MV_FILE_RESPOND);
    QDir dir;
    bool ret = dir.rename(pSrcPath, pTarPath);
    qDebug() << "pSrcPath" << pSrcPath << "pTarPath" << pTarPath << "ret" << ret;
    memcpy(respdu->caData, &ret, sizeof(bool));
    delete[] pSrcPath;
    delete[] pTarPath;
    pSrcPath = NULL;
    pTarPath = NULL;
    return respdu;
}

PDU *MsgHandler::shareFile()
{
    //取出当前用户名和好友数量
    char caCurName[32] = {'\0'};
    int iFriendNum = 0;
    memcpy(caCurName, m_pdu->caData, 32);
    memcpy(&iFriendNum, m_pdu->caData+32, sizeof(int));

    //构建转发pdu，消息类型和接收的类型相同，当前用户名放cadata，文件路径放camsg
    PDU* resendpdu = mkPDU(m_pdu->uiMsgType, m_pdu->uiMsgLen-iFriendNum*32);
    memcpy(resendpdu->caData, caCurName, 32);
    memcpy(resendpdu->caMsg, m_pdu->caMsg+iFriendNum*32, m_pdu->uiMsgLen-iFriendNum*32);
    char caRecvName[32] = {'\0'};
    //转发给每个好友
    for(int i = 0; i < iFriendNum; i++) {
        memcpy(caRecvName, m_pdu->caMsg+i*32, 32);
        MyTcpServer::getInstance().resend(caRecvName, resendpdu);
    }
    free(resendpdu);
    resendpdu = NULL;
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_SHARE_FILE_RESPOND);
    return respdu;
}

PDU *MsgHandler::shareFileAgree()
{
    QString strFilePath = QString(m_pdu->caMsg);
    int index = strFilePath.lastIndexOf('/');
    QString strFileName = strFilePath.right(strFilePath.size() - index - 1);

    QString strRecvPath = QString("%1/%2/%3")
            .arg(Server::getInstance().m_strRootPath)
            .arg(m_pdu->caData)
            .arg(strFileName);
    QFileInfo fileInfo(strFilePath);
    bool ret = true;
    if (fileInfo.isFile()) {
        ret = QFile::copy(strFilePath, strRecvPath);
    } else {
        ret = copyDir(strFilePath, strRecvPath);
    }
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_SHARE_FILE_AGREE_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

bool MsgHandler::copyDir(QString strSrcDir, QString strDestDir)
{
    qDebug() << strSrcDir << strDestDir;
    QDir dir;
    //创建目标目录
    bool ret = dir.mkdir(strDestDir);
    qDebug() << ret;
    dir.setPath(strSrcDir);
    //获取原目录下的文件
    QFileInfoList fList = dir.entryInfoList();

    //遍历原目录下的所有文件，普通文件直接复制，目录递归调用当前函数
    QString srcPath;
    QString destPath;
    for (int i = 0; i < fList.size(); i++) {
        if (fList[i].fileName() == QString(".") || fList[i].fileName() == QString("..")) {
            continue;//.和..跳过
        }
        srcPath = strSrcDir + '/' + fList[i].fileName();
        destPath = strDestDir + '/' + fList[i].fileName();
        if (fList[i].isFile()) {
            ret = QFile::copy(srcPath, destPath);
        } else {
            ret = copyDir(srcPath, destPath);
        }
        qDebug() << ret << fList[i].fileName();
    }
    return ret;
}

PDU *MsgHandler::uploadFileInit()
{
    //文件对象、文件大小、文件已接收大小定义为成员变量
    //取出文件名和文件大小；caData+40 为覆盖标志（1=覆盖重传，0/未设置=尝试续传）
    char caFileName[32] = {'\0'};
    memcpy(caFileName, m_pdu->caData, 32);
    m_iUploadFileSize = 0;
    memcpy(&m_iUploadFileSize, m_pdu->caData+32, sizeof (qint64));
    char chOverwrite = 0;
    memcpy(&chOverwrite, m_pdu->caData+40, sizeof (char));
    //拼接完整路径
    QString strPath = QString("%1/%2").arg(m_pdu->caMsg).arg(caFileName);
    m_fUploadFile.setFileName(strPath);
    //断点续传：服务器上已有同名文件且未要求覆盖时，从已有大小处续写（Append 模式）
    qint64 offset = 0;
    QFileInfo fileInfo(strPath);
    if (fileInfo.exists() && fileInfo.size() > 0 && chOverwrite != 1) {
        offset = fileInfo.size();
        if (offset > m_iUploadFileSize) {
            offset = 0;     //服务器残留文件比待传文件还大（异常情况），从头重传
        }
    }
    m_iReceiveSize = offset;
    bool ret = false;
    if (offset > 0) {
        ret = m_fUploadFile.open(QIODevice::WriteOnly | QIODevice::Append);
    } else {
        ret = m_fUploadFile.open(QIODevice::WriteOnly); //WriteOnly 打开即清空
    }
    qDebug() << "uploadFileInit file" << strPath << "size" << m_iUploadFileSize
             << "resume offset" << offset << "overwrite" << (chOverwrite == 1) << "ret" << ret;
    //应答：caData[0]=是否成功，caData+8=续传偏移量
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_RESPOND);
    memcpy(respdu->caData, &ret, sizeof(bool));
    memcpy(respdu->caData+8, &offset, sizeof(qint64));
    return respdu;
}

PDU *MsgHandler::uploadFileData()
{
    //接收的文件内容写入文件
    m_fUploadFile.write(m_pdu->caMsg, m_pdu->uiMsgLen);
    //更新已接收文件大小并判断是否上传完
    m_iReceiveSize += m_pdu->uiMsgLen;
    if (m_iReceiveSize < m_iUploadFileSize) {
        return NULL;
    }
    m_fUploadFile.close();
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_RESPOND);
    bool ret = m_iReceiveSize == m_iUploadFileSize;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::downloadFile(QTcpSocket* socket)
{
    //caData[0,32) 为文件名，caMsg 为文件所在的服务器目录
    char caFileName[32] = {'\0'};
    memcpy(caFileName, m_pdu->caData, 32);
    QString strPath = QString("%1/%2").arg(QString::fromUtf8(m_pdu->caMsg)).arg(caFileName);

    QFile file(strPath);
    if (!file.open(QIODevice::ReadOnly)) {
        //文件打开失败：应答失败标志
        PDU* respdu = mkPDU(ENUM_MSG_TYPE_DOWNLOAD_FILE_RESPOND);
        bool ret = false;
        memcpy(respdu->caData, &ret, sizeof(bool));
        qDebug() << "downloadFile open failed" << strPath;
        return respdu;
    }
    qint64 fileSize = file.size();
    //应答：caData[0]=成功标志，caData+8=文件总大小
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_DOWNLOAD_FILE_RESPOND);
    bool ret = true;
    memcpy(respdu->caData, &ret, sizeof(bool));
    memcpy(respdu->caData+8, &fileSize, sizeof(qint64));
    socket->write((char*)respdu, respdu->uiPDULen);
    free(respdu);
    respdu = NULL;
    qDebug() << "downloadFile begin" << strPath << "size" << fileSize;

    //分块读取文件内容，逐块推送给客户端
    while (true) {
        PDU* pPdu = mkPDU(ENUM_MSG_TYPE_DOWNLOAD_FILE_DATA_REQUEST, 40960);
        qint64 ret2 = file.read(pPdu->caMsg, 40960);
        if (ret2 <= 0) {
            free(pPdu);
            break;
        }
        //最后一块长度可能不足 40960，需要更新长度字段
        pPdu->uiMsgLen = (uint)ret2;
        pPdu->uiPDULen = (uint)ret2 + sizeof(PDU);
        socket->write((char*)pPdu, pPdu->uiPDULen);
        free(pPdu);
    }
    file.close();
    qDebug() << "downloadFile finished" << strPath;
    return NULL;    //应答和数据已直接发送
}

PDU *MsgHandler::offlineMsg()
{
    //caData[0,32) 为刚登录的用户名
    char caName[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    QString strKey = QString("offline:%1").arg(caName);
    QStringList msgList = RedisClient::getInstance().lrangeAll(strKey);
    qDebug() << "offlineMsg count" << msgList.size() << "for" << caName;

    //caMsg 布局：每条离线消息 = 发送者名[32字节] + 消息长度[int] + 消息内容
    int totalLen = 0;
    foreach (const QString& msg, msgList) {
        //消息格式："发送者:内容"，按第一个':'拆分
        int index = msg.indexOf(':');
        QString text = (index >= 0) ? msg.mid(index+1) : msg;
        totalLen += 32 + sizeof(int) + text.toUtf8().size() + 1;
    }
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_OFFLINE_MSG_RESPOND, totalLen);
    int count = msgList.size();
    memcpy(respdu->caData, &count, sizeof(int));

    int offset = 0;
    foreach (const QString& msg, msgList) {
        int index = msg.indexOf(':');
        QString sender = (index >= 0) ? msg.left(index) : "unknown";
        QString text = (index >= 0) ? msg.mid(index+1) : msg;
        QByteArray baSender = sender.toUtf8();
        QByteArray baText = text.toUtf8();
        int len = baText.size() + 1;
        memcpy(respdu->caMsg+offset, baSender.constData(), 32);
        offset += 32;
        memcpy(respdu->caMsg+offset, &len, sizeof(int));
        offset += sizeof(int);
        memcpy(respdu->caMsg+offset, baText.constData(), len);
        offset += len;
    }
    //消息已全部读出，删除Redis中的离线队列
    if (count > 0) {
        RedisClient::getInstance().del(strKey);
    }
    return respdu;
}


