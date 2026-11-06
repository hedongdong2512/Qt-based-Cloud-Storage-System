#include "redisclient.h"

#include <QTcpSocket>
#include <QDebug>

namespace {

const char* REDIS_HOST = "127.0.0.1";   //Redis 服务器地址
const quint16 REDIS_PORT = 6379;        //Redis 服务器端口
const int CONNECT_TIMEOUT_MS = 1000;    //连接超时
const int READ_TIMEOUT_MS = 2000;       //读超时

//将命令编码为 RESP 协议：*N\r\n $len\r\n<arg>\r\n ...
QByteArray encodeCommand(const QStringList& args)
{
    QByteArray req;
    req.append(QString("*%1\r\n").arg(args.size()).toLatin1());
    foreach (const QString& arg, args) {
        QByteArray baArg = arg.toUtf8();
        req.append(QString("$%1\r\n").arg(baArg.size()).toLatin1());
        req.append(baArg);
        req.append("\r\n");
    }
    return req;
}

//从 socket 缓冲中读取一行（不含\r\n），处理半包
bool readLine(QTcpSocket* socket, QByteArray& buffer, QByteArray& line)
{
    while (true) {
        int index = buffer.indexOf("\r\n");
        if (index >= 0) {
            line = buffer.left(index);
            buffer.remove(0, index + 2);
            return true;
        }
        if (!socket->waitForReadyRead(READ_TIMEOUT_MS)) {
            return false;
        }
        buffer += socket->readAll();
    }
}

//读取一个 bulk 串（$len\r\n<data>\r\n），处理半包
bool readBulk(QTcpSocket* socket, QByteArray& buffer, QByteArray& data)
{
    QByteArray line;
    if (!readLine(socket, buffer, line)) {
        return false;
    }
    if (!line.startsWith('$')) {
        return false;
    }
    int len = line.mid(1).toInt();
    if (len < 0) {          //$-1 表示空串（null bulk）
        data.clear();
        return true;
    }
    while (buffer.size() < len + 2) {
        if (!socket->waitForReadyRead(READ_TIMEOUT_MS)) {
            return false;
        }
        buffer += socket->readAll();
    }
    data = buffer.left(len);
    buffer.remove(0, len + 2);
    return true;
}

//解析一条 RESP 应答，数组应答填入 listReply，其余填入 strReply
bool parseReply(QTcpSocket* socket, QString& strReply, QStringList& strListReply)
{
    QByteArray buffer;
    QByteArray line;
    if (!readLine(socket, buffer, line)) {
        return false;
    }
    char chType = line.isEmpty() ? ' ' : line.at(0);
    if (chType == '+' || chType == '-' || chType == ':') {
        strReply = QString::fromLatin1(line.mid(1));    //简单串/错误/整数
        return chType != '-';
    }
    if (chType != '*') {
        return false;
    }
    int count = line.mid(1).toInt();                    //数组应答
    for (int i = 0; i < count; i++) {
        QByteArray data;
        if (!readBulk(socket, buffer, data)) {
            return false;
        }
        strListReply.append(QString::fromUtf8(data));
    }
    return true;
}

} // namespace

RedisClient& RedisClient::getInstance()
{
    static RedisClient instance;
    return instance;
}

bool RedisClient::exec(const QStringList& args, QString& strReply, QStringList& strListReply)
{
    //短连接：每次命令独立连接，避免跨线程使用同一 socket
    QTcpSocket socket;
    socket.connectToHost(REDIS_HOST, REDIS_PORT);
    if (!socket.waitForConnected(CONNECT_TIMEOUT_MS)) {
        qDebug() << "Redis connect failed:" << socket.errorString();
        return false;
    }
    socket.write(encodeCommand(args));
    if (!socket.waitForBytesWritten(CONNECT_TIMEOUT_MS)) {
        qDebug() << "Redis write failed:" << socket.errorString();
        return false;
    }
    return parseReply(&socket, strReply, strListReply);
}

bool RedisClient::rpush(const QString& strKey, const QString& strValue)
{
    QString strReply;
    QStringList strListReply;
    bool ret = exec(QStringList() << "RPUSH" << strKey << strValue, strReply, strListReply);
    if (!ret) {
        qDebug() << "Redis rpush failed" << strKey << strValue;
    }
    return ret;
}

QStringList RedisClient::lrangeAll(const QString& strKey)
{
    QString strReply;
    QStringList strListReply;
    bool ret = exec(QStringList() << "LRANGE" << strKey << "0" << "-1", strReply, strListReply);
    if (!ret) {
        qDebug() << "Redis lrange failed" << strKey;
        return QStringList();
    }
    return strListReply;
}

bool RedisClient::del(const QString& strKey)
{
    QString strReply;
    QStringList strListReply;
    return exec(QStringList() << "DEL" << strKey, strReply, strListReply);
}
