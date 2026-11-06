#ifndef REDISCLIENT_H
#define REDISCLIENT_H

// 轻量 Redis 客户端：基于 QTcpSocket 直接实现 RESP 协议，
// 不依赖 hiredis 等第三方库，专用于离线消息的存取。
// 每次命令使用独立的短连接，天然线程安全（服务端为多线程池模型）。

#include <QString>
#include <QStringList>

class RedisClient
{
public:
    static RedisClient& getInstance();

    // 离线消息入队：RPUSH offline:<用户名> 消息
    bool rpush(const QString& strKey, const QString& strValue);
    // 取出全部离线消息：LRANGE key 0 -1
    QStringList lrangeAll(const QString& strKey);
    // 取出后删除队列：DEL key
    bool del(const QString& strKey);

private:
    RedisClient() {}
    ~RedisClient() {}
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    // 执行一条命令，reply 为 RESP 解析结果（简单串或数组）
    bool exec(const QStringList& args, QString& strReply, QStringList& strListReply);
};

#endif // REDISCLIENT_H
