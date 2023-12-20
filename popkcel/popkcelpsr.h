/*
Copyright (C) 2020-2023 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef POPKCELPSR_H
#define POPKCELPSR_H

#include "popkcel.h"

#define POPKCEL_PSRVERSION 0

enum Popkcel_PsrState {
    POPKCEL_PS_INIT,
    POPKCEL_PS_CONNECTING,
    POPKCEL_PS_CONNECTED,
    POPKCEL_PS_TRANSFER,
    POPKCEL_PS_CLOSED
};

enum Popkcel_PsrFlag {
    POPKCEL_PF_SINGLE = 1,
    POPKCEL_PF_TRANSFORM = 1 << 1,
    POPKCEL_PF_REPLY = 1 << 2,
    POPKCEL_PF_CLOSED = 1 << 3,
    POPKCEL_PF_SYN = 1 << 4,
    POPKCEL_PF_CONFIRM = 1 << 5,
    POPKCEL_PF_APT = 1 << 7
};

struct Popkcel_PsrSocket;
struct Popkcel_PsrField;

struct Popkcel_RbtnodePsrSend
{
    POPKCEL_RBTFIELD
    struct Popkcel_Timer timer;
    struct Popkcel_PsrField* psr;
    Popkcel_FuncCallback callback;
    void* userData;
    size_t bufLen;
    int sendCount;
    char pending;
    char buffer[];
};

#define POPKCEL_MAXUDPSIZE 1472

struct Popkcel_PsrField;

/// return 0表示继续，非0表示不要继续
typedef int (*Popkcel_PsrRecvCb)(struct Popkcel_PsrSocket* sock, intptr_t rv);
typedef struct Popkcel_PsrField* (*Popkcel_PsrListenCb)(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr);
typedef int (*Popkcel_PsrFuncCallback)(struct Popkcel_PsrField* psr, intptr_t rv);

/// UDP Socket的PSRUDP协议（Popkc's Simple Reliable UDP）部分，可以把udp当成类似tcp来用，主要用于为内网IP之间提供连通能力和可靠传输
struct Popkcel_PsrField
{
    struct Popkcel_Timer timer;
    struct Popkcel_PsrSocket* sock;
    /// rbtree 发送了但还没收到回复确认的数据
    struct Popkcel_Rbtnode* nodePieceSend;
    /// rbtree 接收了的数据，但之前一些序号的数据还没收到
    struct Popkcel_Rbtnode* nodePieceReceive;
    struct Popkcel_Rbtnode* nodeReply;
    /// 对方的IP、端口信息
    struct sockaddr_in6 remoteAddr;
    char buffer[POPKCEL_MAXUDPSIZE];
    char* recvBuf;
    /// 回调函数
    Popkcel_PsrFuncCallback callback;
    Popkcel_FuncCallback lastSendCallback;
    void* userData;
    void* lastSendUserData;
    /// remoteAddr实际内容的长度
    socklen_t addrLen;
    uint32_t bufferPos;
    /// 本机当前的发送序号
    uint32_t mySendId;
    /// 对方当前的发送序号
    uint32_t othersSendId;
    uint32_t lastMyConfirmedSendId;
    /// 防止传输被除中间方外的第三方伪造的id
    char tranId[4];
    char tranIdNew[4];
    /// 此连接的窗口数
    uint16_t window;
    /// psrudp连接状态
    char state;
    char needSend;
    char timerStarted;
    char synConfirm;
};

#define POPKCEL_PSRSOCKETFIELD           \
    char psrBuffer[POPKCEL_MAXUDPSIZE];  \
    struct Popkcel_Timer timerKeepAlive; \
    int64_t lastSendTime;                \
    Popkcel_PsrListenCb listenCb;        \
    Popkcel_PsrRecvCb recvCb;            \
    struct sockaddr_in6 remoteAddr;      \
    struct Popkcel_Rbtnode* nodePf;      \
    void* userData;                      \
    socklen_t remoteAddrLen;             \
    char tranId[4];                      \
    uint32_t recvLen;                    \
    uint16_t maxWindow;                  \
    uint16_t psrWindow;

struct Popkcel_PsrSocket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETCOMMONFIELD
    POPKCEL_SOCKETFIELD
    POPKCEL_PSRSOCKETFIELD
};

LIBPOPKCEL_EXTERN int popkcel_initPsrSocket(struct Popkcel_PsrSocket* sock, struct Popkcel_Loop* loop, Popkcel_HandleType fd, char ipv6, uint16_t port, Popkcel_PsrListenCb listenCb, Popkcel_PsrRecvCb recvCb, uint16_t maxWindow);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrSocket(struct Popkcel_PsrSocket* sock);

LIBPOPKCEL_EXTERN void popkcel_initPsrField(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrField(struct Popkcel_PsrField* psr);

LIBPOPKCEL_EXTERN int popkcel_psrTryConnect(struct Popkcel_PsrField* psr);

LIBPOPKCEL_EXTERN int popkcel_psrTrySend(struct Popkcel_PsrField* psr, const char* data, size_t len, Popkcel_FuncCallback callback, void* userData);

LIBPOPKCEL_EXTERN void popkcel_psrAcceptOne(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc);

LIBPOPKCEL_EXTERN int popkcel_psrSendCache(struct Popkcel_PsrField* psr);

#endif