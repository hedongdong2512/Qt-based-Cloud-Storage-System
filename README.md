# Qt-based-Cloud-Storage-System

基于 Qt/C++ 与 TCP 自定义协议实现的云盘系统，C/S 架构，服务端支持多客户端并发连接，提供注册登录、好友管理、在线/离线聊天、文件管理与分享、断点续传上传、文件下载等功能。

## 功能

- 用户模块：注册、登录、上线/下线状态维护
- 好友模块：查找用户、添加/同意/删除好友、在线好友刷新
- 聊天模块：点对点消息；目标不在线时由服务端写入 **Redis 离线消息队列**，对方上线登录后自动拉取送达
- 文件模块：新建/删除目录、重命名、移动文件、刷新文件列表、递归复制目录实现文件分享
- 文件上传：客户端独立线程分块读取发送，服务端累积接收落盘；支持**断点续传**——INIT 时服务端返回同名文件已收到的字节数（offset），客户端 `seek(offset)` 续传剩余部分，也可选择覆盖重传
- 文件下载：服务端分块读取推送，客户端累积接收写盘

## 通信协议

自定义协议数据单元 PDU（Protocol Data Unit）：

```cpp
struct PDU {
    uint uiPDULen;   // 总长度
    uint uiMsgLen;   // 消息体长度
    uint uiMsgType;  // 消息类型（注册/登录/聊天/上传/下载等）
    char caData[64]; // 参数区
    char caMsg[];    // 柔性数组，按需分配消息体
};
```

- 使用柔性数组 `caMsg` 将协议头与消息体一次分配，减少内存碎片与多余拷贝；
- TCP 粘包/半包处理：接收端以 `QByteArray buffer` 累积数据，按 `uiPDULen` 定长拆包，循环处理缓冲区中的完整包。

## 架构

```
Client (Qt GUI)                    Server (Qt 服务端)
  ├─ Client      单例，socket 封装    ├─ MyTcpServer   单例，QTcpServer + QThreadPool
  ├─ ResHandler  响应分发             ├─ ClientTask    每连接一个任务，moveToThread
  ├─ File/Friend/Chat 等界面          ├─ MyTcpSocket   会话封装，收包/拆包/分发
  └─ Uploader    独立线程分块上传      ├─ MsgHandler    按消息类型处理业务
                                     ├─ OperateDB     单例，MySQL 访问
                                     └─ RedisClient   轻量 Redis 客户端（离线消息）
```

服务端通过 `incomingConnection` 接入新连接，交给 `QThreadPool` 中的 `ClientTask` 处理；消息转发时遍历在线 socket 列表按登录名匹配目标连接，未找到目标则进入离线消息流程。

## Redis 离线消息

`Server/redisclient.h/.cpp` 基于 `QTcpSocket` 直接实现 RESP 协议的最小客户端（RPUSH / LRANGE / DEL），不依赖 hiredis 等第三方库：

- 聊天目标不在线：服务端执行 `RPUSH offline:<目标用户名> "发送者:消息"`；
- 用户登录成功：客户端发送 `OFFLINE_MSG_REQUEST`，服务端 `LRANGE` 取出全部离线消息封装进一个应答 PDU，随后 `DEL` 删除队列；
- 每次命令使用独立短连接，与多线程服务端模型天然兼容（无跨线程 socket 复用问题）。

运行前需启动 Redis 服务（默认 `127.0.0.1:6379`）。

## 数据库

MySQL 存储 `user_info`（用户）与 `friend`（好友关系，双向记录）两张表，通过子查询 + union 实现好友查询。

连接配置在 `Server/operatedb.cpp`，数据库密码从环境变量 `DB_PASSWORD` 读取（避免硬编码）：

```
set DB_PASSWORD=your_password   # Windows
export DB_PASSWORD=your_password # Linux
```

建表语句：

```sql
create database mydb2404;
use mydb2404;
create table user_info(id int primary key auto_increment, name varchar(32) not null, pwd varchar(32) not null, online int default 0);
create table friend(id int primary key auto_increment, user_id int, friend_id int);
```

## 测试

`test/protocol_test.cpp` 为协议级自动化测试客户端，验证三项核心功能：

1. 离线消息：A 给不在线的 B 发消息 → Redis 落队 → B 登录拉取送达；
2. 断点续传：上传 100KB 文件中途断开 → 重连后从 offset=51200 续传 → 服务端文件完整；
3. 文件下载：下载上传的文件并逐字节比对内容一致。

## 构建与运行

- 环境：Qt 5.x（network、sql 模块）、MySQL、Redis、C++11
- 分别用 Qt Creator 打开 `Client/Client.pro` 与 `Server/Server.pro` 构建运行
- `socket.config` 存放服务器地址与端口（默认 `127.0.0.1:5000`），服务端根目录为 `./filesys`
