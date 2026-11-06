// 协议级自动化测试客户端：验证离线消息、断点续传、下载
// 控制台程序，直接以 PDU 协议与 Server 通信
#include <QCoreApplication>
#include <QTcpSocket>
#include <QFile>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>

//将Qt日志输出重定向到stderr，保证重定向到文件时也能看到
static void testMsgHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    fflush(stderr);
}

#include "protocol.h"

static QTcpSocket g_socket;
static QByteArray g_buffer;

static bool sendPDU(PDU* pdu)
{
    g_socket.write((char*)pdu, pdu->uiPDULen);
    g_socket.waitForBytesWritten(2000);
    free(pdu);
    return true;
}

static PDU* mkPDU2(uint type, uint msgLen = 0)
{
    PDU* pdu = (PDU*)malloc(sizeof(PDU) + msgLen);
    memset(pdu, 0, sizeof(PDU) + msgLen);
    pdu->uiPDULen = sizeof(PDU) + msgLen;
    pdu->uiMsgLen = msgLen;
    pdu->uiMsgType = type;
    return pdu;
}

//阻塞式收一个完整 PDU
static PDU* recvPDU(int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        while (g_buffer.size() >= int(sizeof(PDU))) {
            PDU* head = (PDU*)g_buffer.data();
            if (g_buffer.size() < int(head->uiPDULen)) break;
            PDU* pdu = mkPDU2(head->uiMsgType, head->uiMsgLen);
            memcpy(pdu, head, head->uiPDULen);
            g_buffer.remove(0, head->uiPDULen);
            return pdu;
        }
        g_socket.waitForReadyRead(500);
        g_buffer += g_socket.readAll();
    }
    return NULL;
}

static bool waitRespond(uint type, PDU** out, int timeoutMs = 10000)
{
    PDU* pdu = NULL;
    while ((pdu = recvPDU(timeoutMs)) != NULL) {
        qDebug() << "recv type" << pdu->uiMsgType << "msgLen" << pdu->uiMsgLen;
        if (pdu->uiMsgType == type) {
            *out = pdu;
            return true;
        }
        free(pdu);
    }
    return false;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(testMsgHandler);

    //======== 1. 连接 ========
    g_socket.connectToHost("127.0.0.1", 5000);
    if (!g_socket.waitForConnected(5000)) {
        qDebug() << "FAIL: connect server" << g_socket.errorString();
        return 1;
    }
    qDebug() << "PASS: connected";

    //======== 2. 注册并登录两个用户 ========
    auto registAndLogin = [](const char* name, const char* pwd) {
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_REGIST_REQUEST);
        memcpy(pdu->caData, name, strlen(name));
        memcpy(pdu->caData+32, pwd, strlen(pwd));
        sendPDU(pdu);
        PDU* resp = NULL;
        if (!waitRespond(ENUM_MSG_TYPE_REGIST_RESPOND, &resp)) { qDebug() << "no regist respond"; return false; }
        bool ok; memcpy(&ok, resp->caData, sizeof(bool)); free(resp);
        pdu = mkPDU2(ENUM_MSG_TYPE_LOGIN_REQUEST);
        memcpy(pdu->caData, name, strlen(name));
        memcpy(pdu->caData+32, pwd, strlen(pwd));
        sendPDU(pdu);
        if (!waitRespond(ENUM_MSG_TYPE_LOGIN_RESPOND, &resp)) { qDebug() << "no login respond"; return false; }
        memcpy(&ok, resp->caData, sizeof(bool)); free(resp);
        return ok;
    };
    if (!registAndLogin("testa", "123")) { qDebug() << "FAIL: testa login"; return 1; }
    qDebug() << "PASS: testa logged in";

    //======== 3. 离线消息：向未登录的 testb 发消息 ========
    {
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_CHAT_REQUEST, 64);
        memcpy(pdu->caData, "testa", 5);
        memcpy(pdu->caData+32, "testb", 5);
        const char* msg = "hello from testa offline";
        memcpy(pdu->caMsg, msg, strlen(msg)+1);
        sendPDU(pdu);
        PDU* resp = NULL;
        if (waitRespond(ENUM_MSG_TYPE_CHAT_RESPOND, &resp, 5000)) {
            qDebug() << "PASS: chat respond:" << resp->caMsg;
            free(resp);
        } else {
            qDebug() << "FAIL: no chat respond for offline target";
        }
    }
    qDebug() << "check redis now (offline:testb should exist)";

    //======== 4. testb 第二条连接登录并拉取离线消息 ========
    QTcpSocket socketB;
    socketB.connectToHost("127.0.0.1", 5000);
    if (!socketB.waitForConnected(5000)) { qDebug() << "FAIL: connect B"; return 1; }
    //切换收发上下文到 B
    //简单做法：B 只收 LOGIN_RESPOND 与 OFFLINE_MSG_RESPOND
    {
        //先注册 testb（离线消息目标用户必须存在）
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_REGIST_REQUEST);
        memcpy(pdu->caData, "testb", 5);
        memcpy(pdu->caData+32, "123", 3);
        socketB.write((char*)pdu, pdu->uiPDULen);
        socketB.waitForBytesWritten(2000);
        free(pdu);
        //把当前 g_socket 的收包先清掉（testa 的 online user 推送等）
        pdu = mkPDU2(ENUM_MSG_TYPE_LOGIN_REQUEST);
        memcpy(pdu->caData, "testb", 5);
        memcpy(pdu->caData+32, "123", 3);
        socketB.write((char*)pdu, pdu->uiPDULen);
        socketB.waitForBytesWritten(2000);
        free(pdu);
        //读 B 的 LOGIN_RESPOND（跳过其它推送）
        QElapsedTimer timer; timer.start();
        QByteArray bufB;
        bool loginOk = false;
        while (timer.elapsed() < 8000) {
            socketB.waitForReadyRead(300);
            bufB += socketB.readAll();
            while (bufB.size() >= int(sizeof(PDU))) {
                PDU* head = (PDU*)bufB.data();
                if (bufB.size() < int(head->uiPDULen)) break;
                if (head->uiMsgType == ENUM_MSG_TYPE_LOGIN_RESPOND) {
                    loginOk = *(bool*)head->caData;
                    bufB.remove(0, head->uiPDULen);
                    goto login_done;
                }
                bufB.remove(0, head->uiPDULen);
            }
        }
login_done:
        if (!loginOk) { qDebug() << "FAIL: testb login"; return 1; }
        qDebug() << "PASS: testb logged in";

        //拉取离线消息
        PDU* pdu2 = mkPDU2(ENUM_MSG_TYPE_OFFLINE_MSG_REQUEST);
        memcpy(pdu2->caData, "testb", 5);
        socketB.write((char*)pdu2, pdu2->uiPDULen);
        socketB.waitForBytesWritten(2000);
        free(pdu2);
        timer.restart();
        bool gotOffline = false;
        int count = 0;
        while (timer.elapsed() < 8000) {
            socketB.waitForReadyRead(300);
            bufB += socketB.readAll();
            while (bufB.size() >= int(sizeof(PDU))) {
                PDU* head = (PDU*)bufB.data();
                if (bufB.size() < int(head->uiPDULen)) break;
                if (head->uiMsgType == ENUM_MSG_TYPE_OFFLINE_MSG_RESPOND) {
                    memcpy(&count, head->caData, sizeof(int));
                    qDebug() << "PASS: offline msgs received, count =" << count;
                    //解析第一条的发送者与内容
                    if (count > 0) {
                        char sender[33] = {'\0'};
                        memcpy(sender, head->caMsg, 32);
                        int len; memcpy(&len, head->caMsg+32, sizeof(int));
                        char text[256] = {'\0'};
                        memcpy(text, head->caMsg+36, qMin(len, 255));
                        qDebug() << "offline msg from" << sender << "text" << text;
                    }
                    gotOffline = true;
                    break;
                }
                bufB.remove(0, head->uiPDULen);
            }
            if (gotOffline) break;
        }
        if (!gotOffline) { qDebug() << "FAIL: no offline msg respond"; return 1; }
    }
    socketB.disconnectFromHost();

    //======== 5. 断点续传 + 下载：testa 上传 100KB 文件，只传一半断开，再续传 ========
    const qint64 TOTAL = 100*1024;
    QByteArray fileData(TOTAL, 0);
    for (int i = 0; i < TOTAL; i++) fileData[i] = char(i % 251);   //确定性内容
    QString uploadDir = QString("./filesys/testa");
    QString srvFile = uploadDir + "/testfile.bin";

    //第一次 INIT（会收到续传提示，如果之前残留则询问逻辑在GUI端；测试端直接续传）
    qint64 firstSent = 0;   //第一次实际发送的字节数
    qint64 offsetGot = 0;
    {
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_REQUEST, uploadDir.size()+1);
        memcpy(pdu->caData, "testfile.bin", 12);
        memcpy(pdu->caData+32, &TOTAL, sizeof(qint64));
        char chOverwrite = 1;               //第一次：覆盖旧残留，从0开始
        memcpy(pdu->caData+40, &chOverwrite, 1);
        memcpy(pdu->caMsg, uploadDir.toStdString().c_str(), uploadDir.size());
        sendPDU(pdu);
        PDU* resp = NULL;
        if (!waitRespond(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_RESPOND, &resp)) { qDebug() << "FAIL: init1"; return 1; }
        memcpy(&offsetGot, resp->caData+8, sizeof(qint64)); free(resp);
        qDebug() << "init1 offset =" << offsetGot << "(expect 0)";
        //只发送一半
        qint64 half = TOTAL/2;
        PDU* dp = mkPDU2(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_REQUEST, half);
        memcpy(dp->caMsg, fileData.constData(), half);
        sendPDU(dp);
        firstSent = half;
        //服务器不会回 DATA_RESPOND（没传完），直接断开模拟中断
        g_socket.disconnectFromHost();
        g_socket.waitForDisconnected(3000);
    }
    //重连续传
    g_socket.connectToHost("127.0.0.1", 5000);
    if (!g_socket.waitForConnected(5000)) { qDebug() << "FAIL: reconnect"; return 1; }
    qint64 offset2 = 0;
    {
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_REQUEST, uploadDir.size()+1);
        memcpy(pdu->caData, "testfile.bin", 12);
        memcpy(pdu->caData+32, &TOTAL, sizeof(qint64));
        char chOverwrite = 0;               //第二次：尝试续传
        memcpy(pdu->caData+40, &chOverwrite, 1);
        memcpy(pdu->caMsg, uploadDir.toStdString().c_str(), uploadDir.size());
        sendPDU(pdu);
        PDU* resp = NULL;
        if (!waitRespond(ENUM_MSG_TYPE_UPLOAD_FILE_INIT_RESPOND, &resp)) { qDebug() << "FAIL: init2"; return 1; }
        memcpy(&offset2, resp->caData+8, sizeof(qint64)); free(resp);
    }
    qDebug() << "resume offset =" << offset2 << "(expect" << firstSent << ")";
    if (offset2 != firstSent) {
        qDebug() << "FAIL: resume offset mismatch";
        return 1;
    }
    qDebug() << "PASS: resume offset correct";
    //从 offset2 续传剩余
    {
        qint64 remain = TOTAL - offset2;
        PDU* dp = mkPDU2(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_REQUEST, remain);
        memcpy(dp->caMsg, fileData.constData()+offset2, remain);
        sendPDU(dp);
        PDU* resp = NULL;
        if (!waitRespond(ENUM_MSG_TYPE_UPLOAD_FILE_DATA_RESPOND, &resp)) { qDebug() << "FAIL: data respond"; return 1; }
        bool ok; memcpy(&ok, resp->caData, sizeof(bool)); free(resp);
        if (!ok) { qDebug() << "FAIL: upload not ok"; return 1; }
        qDebug() << "PASS: resume upload completed";
    }

    //======== 6. 下载并校验内容 ========
    {
        PDU* pdu = mkPDU2(ENUM_MSG_TYPE_DOWNLOAD_FILE_REQUEST, uploadDir.size()+1);
        memcpy(pdu->caData, "testfile.bin", 12);
        memcpy(pdu->caMsg, uploadDir.toStdString().c_str(), uploadDir.size());
        sendPDU(pdu);
        //收：DOWNLOAD_FILE_RESPOND + N 个 DATA
        QByteArray downloaded;
        bool gotInit = false;
        bool done = false;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 15000) {
            g_socket.waitForReadyRead(500);
            g_buffer += g_socket.readAll();
            while (g_buffer.size() >= int(sizeof(PDU))) {
                PDU* head = (PDU*)g_buffer.data();
                if (g_buffer.size() < int(head->uiPDULen)) break;
                if (head->uiMsgType == ENUM_MSG_TYPE_DOWNLOAD_FILE_RESPOND) {
                    bool ok = *(bool*)head->caData;
                    if (!ok) { qDebug() << "FAIL: download init not ok"; return 1; }
                    qint64 sz; memcpy(&sz, head->caData+8, sizeof(qint64));
                    qDebug() << "download init size" << sz;
                    gotInit = true;
                } else if (head->uiMsgType == ENUM_MSG_TYPE_DOWNLOAD_FILE_DATA_REQUEST) {
                    downloaded.append(head->caMsg, head->uiMsgLen);
                    if (downloaded.size() >= TOTAL) { done = true; }
                }
                g_buffer.remove(0, head->uiPDULen);
                if (done) break;
            }
            if (done) break;
        }
        if (!gotInit || !done) { qDebug() << "FAIL: download incomplete" << downloaded.size(); return 1; }
        if (downloaded != fileData) {
            qDebug() << "FAIL: download content mismatch";
            return 1;
        }
        qDebug() << "PASS: download content verified, size" << downloaded.size();
    }

    qDebug() << "ALL TESTS PASSED";
    return 0;
}
