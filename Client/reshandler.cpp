#include "client.h"
#include "index.h"
#include "reshandler.h"
#include "uploader.h"
#include "protocol.h"
#include "file.h"
#include "chat.h"

#include <QMessageBox>

ResHandler::ResHandler()
{

}

void ResHandler::regist()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        QMessageBox::information(&Client::getInstance(), "注册", "注册成功");
    } else {
        QMessageBox::information(&Client::getInstance(), "注册", "注册失败");
    }
}

void ResHandler::login(QString strLoginName)
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        Index::getInstance().show();
        Index::getInstance().setWindowTitle(strLoginName);
        Client::getInstance().hide();
        //登录成功后主动拉取Redis离线消息
        PDU* pdu = mkPDU(ENUM_MSG_TYPE_OFFLINE_MSG_REQUEST);
        memcpy(pdu->caData, strLoginName.toStdString().c_str(), 32);
        Client::getInstance().sendMsg(pdu);
    } else {
        QMessageBox::information(&Client::getInstance(), "登录", "登录失败");
    }
}

void ResHandler::findUser()
{
    char caName[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    if (Client::getInstance().m_strLoginName == caName) {
        QMessageBox::information(&Index::getInstance(), "搜索", "这个大帅哥竟然是我自己");
        return;
    }
    int ret;
    memcpy(&ret, m_pdu->caData+32, sizeof(int));
    if (ret == -1) {
        QMessageBox::information(&Index::getInstance(), "搜索", "查找用户失败");
    } else if (ret == 0) {
        QMessageBox::information(&Index::getInstance(), "搜索", QString("'%1' 不在线").arg(caName));
    } else if (ret == 1) {
        int ret = QMessageBox::information(&Index::getInstance(), "搜索", QString("'%1' 在线").arg(caName), "添加好友", "取消");
        if (ret == 0) {
            //获取登录的用户名和选择要添加的用户名
            QString strCurName = Client::getInstance().m_strLoginName;
            QString strTarName = QString(caName);

            //构建pdu，将两个用户名放入pdu并发送给服务器
            PDU* pdu = mkPDU(ENUM_MSG_TYPE_ADD_FRIEND_REQUEST);
            memcpy(pdu->caData, strCurName.toStdString().c_str(), 32);
            memcpy(pdu->caData+32, strTarName.toStdString().c_str(), 32);
            Client::getInstance().sendMsg(pdu);
        }
    } else if (ret == 2) {
        QMessageBox::information(&Index::getInstance(), "搜索", QString("'%1' 不存在").arg(caName));
    }
}

void ResHandler::onlineUser(QString strLoginName)
{
    uint uiSize = m_pdu->uiMsgLen/32;
    QStringList userList;
    char caTmp[32] = {'\0'};
    for (uint i = 0; i < uiSize; i++) {
        memcpy(caTmp, m_pdu->caMsg+i*32, 32);
        if (QString(caTmp) == strLoginName) {
            continue;
        }
        userList.append(QString(caTmp));
    }
    Index::getInstance().getFriend()->m_pOnlineUser->update_LW(userList);
}

void ResHandler::addFriend()
{
    int ret;
    memcpy(&ret, m_pdu->caData, sizeof(int));
    if (ret == -1) {
        QMessageBox::information(&Index::getInstance(), "搜索", "添加好友错误");
    } else if (ret == 0) {
        QMessageBox::information(&Index::getInstance(), "搜索", "该用户不在线");
    } else if (ret == -2) {
        QMessageBox::information(&Index::getInstance(), "搜索", "该用户已经是你的好友");
    }
}

void ResHandler::addFriendResend()
{
    char caName[32] = {'\0'};
    memcpy(caName, m_pdu->caData, 32);
    int ret = QMessageBox::question(&Index::getInstance(), "添加好友", QString("是否同意 %1 的添加好友请求？").arg(caName));
    if (ret != QMessageBox::Yes) {
        return;
    }
    PDU* respdu = mkPDU(ENUM_MSG_TYPE_ADD_FRIEND_AGREE_REQUEST);
    memcpy(respdu->caData, m_pdu->caData, 64);
    Client::getInstance().sendMsg(respdu);
}

void ResHandler::addFriendAgree()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "添加好友", "添加好友成功");
        Index::getInstance().getFriend()->flushFriend();
    } else {
        QMessageBox::information(&Index::getInstance(), "添加好友", "添加好友失败");
    }
}

void ResHandler::flushFriend()
{
    uint uiSize = m_pdu->uiMsgLen/32;
    QStringList userList;
    char caTmp[32] = {'\0'};
    for (uint i = 0; i < uiSize; i++) {
        memcpy(caTmp, m_pdu->caMsg+i*32, 32);
        userList.append(QString(caTmp));
    }
    Index::getInstance().getFriend()->update_LW(userList);
}

void ResHandler::delFriend()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (!ret) {
        QMessageBox::information(&Index::getInstance(), "删除好友", "删除好友失败");
    }
    Index::getInstance().getFriend()->flushFriend();
}

void ResHandler::chat()
{
    Chat* c = Index::getInstance().getFriend()->m_pChat;
    if (c->isHidden()) {
        c->show();
    }
    char caChatName[32] = {'\0'};
    memcpy(caChatName, m_pdu->caData, 32);
    c->updateShow_TE(QString("%1: %2").arg(caChatName).arg(m_pdu->caMsg));
    c->setWindowTitle(QString("to: %1").arg(caChatName));
    c->m_strChatName = caChatName;
}

void ResHandler::chatRespond()
{
    //发送离线消息/聊天后的服务器应答，caMsg 为提示文本
    QMessageBox::information(&Index::getInstance(), "聊天", QString(m_pdu->caMsg));
}

void ResHandler::mkdir()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "创建文件夹", "创建文件夹失败");
    }
}

void ResHandler::flushFile()
{
    int iCount = m_pdu->uiMsgLen / sizeof(FileInfo);
    qDebug() << "file count" << iCount;
    QList<FileInfo*> pFileList;
    for (int i = 0; i < iCount; i++) {
        FileInfo* pFileInfo = new FileInfo;
        memcpy(pFileInfo, m_pdu->caMsg+i*sizeof(FileInfo), sizeof(FileInfo));
        pFileList.append(pFileInfo);
    }
    Index::getInstance().getFile()->updateFileList(pFileList);
}

void ResHandler::delDir()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "删除文件夹", "删除文件夹失败");
    }
}

void ResHandler::renameFile()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "重命名文件", "重命名文件失败");
    }
}

void ResHandler::mvFile()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "移动文件", "移动文件失败");
    }
}

void ResHandler::shareFile()
{
    QMessageBox::information(&Index::getInstance(), "分享文件", "文件已分享");
}

void ResHandler::shareFileResend()
{
    QString strFilePath = QString(m_pdu->caMsg);
    int index = strFilePath.lastIndexOf('/');
    QString strFileName = strFilePath.right(strFilePath.size() - index - 1);

    int ret = QMessageBox::question(
                &Index::getInstance(),
                "分享文件",
                QString("%1 分享文件 %2\n是否接收？").arg(m_pdu->caData).arg(strFileName));
    if (ret != QMessageBox::Yes) {
        return;
    }
    PDU* pdu = mkPDU(ENUM_MSG_TYPE_SHARE_FILE_AGREE_REQUEST, m_pdu->uiMsgLen);
    memcpy(pdu->caData, Client::getInstance().m_strLoginName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_pdu->caMsg, pdu->uiMsgLen);
    Client::getInstance().sendMsg(pdu);
}

void ResHandler::shareFileAgree()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "分享文件", "分享文件成功");
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "分享文件", "分享文件失败");
    }
}

void ResHandler::uploadFileInit()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (!ret) {
        QMessageBox::information(&Index::getInstance(), "上传文件", "上传文件初始化失败");
        return;
    }
    //服务器返回续传偏移量：表示服务器上已有同名文件的部分内容
    qint64 offset = 0;
    memcpy(&offset, m_pdu->caData+8, sizeof(qint64));
    File* pFile = Index::getInstance().getFile();
    QFile file(pFile->m_strUploadFilePath);
    qint64 fileSize = file.size();
    if (offset > 0 && offset < fileSize) {
        //询问用户：从断点续传，还是放弃已有部分从头重传（覆盖）
        int choice = QMessageBox::question(
                    &Index::getInstance(), "上传文件",
                    QString("服务器已有同名文件，已传 %1/%2 字节\n"
                            "选择\"Yes\"从断点续传，\"No\"覆盖重传")
                    .arg(offset).arg(fileSize));
        if (choice != QMessageBox::Yes) {
            //覆盖重传：重新发送 INIT，带覆盖标志
            PDU* pdu = mkPDU(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_REQUEST,
                             pFile->m_strCurPath.toStdString().size()+1);
            int index = pFile->m_strUploadFilePath.lastIndexOf('/');
            QString strFileName = pFile->m_strUploadFilePath.right(
                        pFile->m_strUploadFilePath.size() - index - 1);
            memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
            memcpy(pdu->caData+32, &fileSize, sizeof(qint64));
            char chOverwrite = 1;
            memcpy(pdu->caData+40, &chOverwrite, sizeof(char));
            memcpy(pdu->caMsg, pFile->m_strCurPath.toStdString().c_str(),
                   pFile->m_strCurPath.toStdString().size());
            Client::getInstance().sendMsg(pdu);
            return;
        }
    }
    pFile->m_iUploadOffset = offset;
    Client::getInstance().startUpload();
}

void ResHandler::uploadFileData()
{
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "上传文件", "上传文件成功");
    } else {
        QMessageBox::information(&Index::getInstance(), "上传文件", "上传文件初始化失败");
    }
    Index::getInstance().getFile()->flushFile();
}

void ResHandler::downloadFileInit()
{
    //caData[0]=成功标志，caData+8=文件总大小
    bool ret;
    memcpy(&ret, m_pdu->caData, sizeof(bool));
    if (!ret) {
        QMessageBox::information(&Index::getInstance(), "下载文件", "下载文件初始化失败");
        return;
    }
    qint64 fileSize;
    memcpy(&fileSize, m_pdu->caData+8, sizeof(qint64));
    //打开本地保存文件并记录进度
    File* pFile = Index::getInstance().getFile();
    m_fDownloadFile.setFileName(pFile->m_strDownloadSavePath);
    if (!m_fDownloadFile.open(QIODevice::WriteOnly)) {
        QMessageBox::information(&Index::getInstance(), "下载文件", "本地保存文件打开失败");
        return;
    }
    m_iDownloadFileSize = fileSize;
    m_iDownloadReceiveSize = 0;
    qDebug() << "downloadFileInit size" << fileSize;
}

void ResHandler::downloadFileData()
{
    //未初始化成功时不处理（比如本地文件打开失败）
    if (!m_fDownloadFile.isOpen()) {
        return;
    }
    m_fDownloadFile.write(m_pdu->caMsg, m_pdu->uiMsgLen);
    m_iDownloadReceiveSize += m_pdu->uiMsgLen;
    qDebug() << "downloadFileData received" << m_iDownloadReceiveSize
             << "/" << m_iDownloadFileSize;
    if (m_iDownloadReceiveSize >= m_iDownloadFileSize) {
        m_fDownloadFile.close();
        QMessageBox::information(&Index::getInstance(), "下载文件", "下载文件成功");
    }
}

void ResHandler::offlineMsg()
{
    //caData[0,4)=消息条数；caMsg 每条 = 发送者[32] + 消息长度[int] + 消息内容
    int count = 0;
    memcpy(&count, m_pdu->caData, sizeof(int));
    if (count <= 0) {
        return;
    }
    int offset = 0;
    for (int i = 0; i < count; i++) {
        char caSender[32] = {'\0'};
        memcpy(caSender, m_pdu->caMsg+offset, 32);
        offset += 32;
        int len = 0;
        memcpy(&len, m_pdu->caMsg+offset, sizeof(int));
        offset += sizeof(int);
        char* caText = new char[len];
        memcpy(caText, m_pdu->caMsg+offset, len);
        offset += len;
        //复用聊天窗口显示离线消息
        Chat* c = Index::getInstance().getFriend()->m_pChat;
        if (c->isHidden()) {
            c->show();
        }
        c->updateShow_TE(QString("%1(离线): %2").arg(caSender).arg(caText));
        c->setWindowTitle(QString("to: %1").arg(caSender));
        c->m_strChatName = caSender;
        delete[] caText;
    }
    QMessageBox::information(&Index::getInstance(), "离线消息",
                             QString("你有 %1 条离线消息已送达").arg(count));
}
