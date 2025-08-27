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

#ifdef __cplusplus
extern "C" {
#endif

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
    struct Popkcel_PsrField *psr;
    Popkcel_FuncCallback callback;
    void *userData;
    size_t bufLen;
    int sendCount;
    char pending;
    char buffer[];
};

#define POPKCEL_MAXUDPSIZE 1472

struct Popkcel_PsrField;

/// return 0表示继续，非0表示不要继续
typedef int (*Popkcel_PsrRecvCb)(struct Popkcel_PsrSocket *sock, intptr_t rv);
/** 在创建过程中listenCb会调用两次，第一次psr为NULL，用户需要自行malloc并用psrAcceptOne初始化psrField，然后返回创建的psr。
 *  当psr加入到sock的map中后，会再次调用此回调函数，此时psr参数为用户之前创建的psr。
 *  第二次调用时，用户才可以通过网络发送初始的信息，第一次调用时，用户不可以发送任何包。这么设计是方便用户在第一次调用时判断对方的地址是否符合要求，如果不符合，则通过listenCb返回NULL，来避免回复确认信息和不必要的把此psrField加入map中。
 *  */
typedef struct Popkcel_PsrField *(*Popkcel_PsrListenCb)(struct Popkcel_PsrSocket *sock, struct Popkcel_PsrField *psr);
typedef int (*Popkcel_PsrFuncCallback)(struct Popkcel_PsrField *psr, intptr_t rv);

/// UDP Socket的PSRUDP协议（Popkc's Simple Reliable UDP）部分，可以把udp当成类似tcp来用，主要用于为内网IP之间提供连通能力和可靠传输
struct Popkcel_PsrField
{
    struct Popkcel_Timer timer;
    struct Popkcel_PsrSocket *sock;
    /// rbtree 发送了但还没收到回复确认的数据
    struct Popkcel_Rbtnode *nodePieceSend;
    /// rbtree 接收了的数据，但之前一些序号的数据还没收到
    struct Popkcel_Rbtnode *nodePieceReceive;
    struct Popkcel_Rbtnode *nodeReply;
    /// 对方的IP、端口信息
    struct sockaddr_in6 remoteAddr;
    char buffer[POPKCEL_MAXUDPSIZE];
    char *recvBuf;
    /** 回调函数，rv传正值表示收到的字节数，此时recvBuf指向收到的数据。rv传负值表示对应的POPKCEL_CONNECTED、POPKCEL_ERROR等事件。
     * 此函数返回非0值表示此psrField已删除，不要再进行后续操作。
     */
    Popkcel_PsrFuncCallback callback;
    Popkcel_FuncCallback lastSendCallback;
    // 此变量是给用户用的,不是给库内部用的
    void *userData;
    void *lastSendUserData;
    /// remoteAddr实际内容的长度
    socklen_t addrLen;
    uint32_t bufferPos;
    /// 本机当前的发送序号
    uint32_t mySendId;
    /// 对方当前的发送序号
    uint32_t oppositeSendId;
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
    struct Popkcel_Rbtnode *nodePf;      \
    void *userData;                      \
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

/**
 * @param maxWindow 最大允许的不连续的包的数量.网络传输过程中可能会掉包,导致包的到达顺序不同,maxWindow就是这些非连续的包所允许的最大数量,超过这个数量的话,新包将被丢弃,直到缺失的包传到为止.
 */
LIBPOPKCEL_EXTERN int popkcel_initPsrSocket(struct Popkcel_PsrSocket *sock, struct Popkcel_Loop *loop, Popkcel_HandleType fd, char ipv6, uint16_t port, Popkcel_PsrListenCb listenCb, Popkcel_PsrRecvCb recvCb, uint16_t maxWindow);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrSocket(struct Popkcel_PsrSocket *sock);

LIBPOPKCEL_EXTERN void popkcel_initPsrField(struct Popkcel_PsrSocket *sock, struct Popkcel_PsrField *psr, Popkcel_PsrFuncCallback cbFunc);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrField(struct Popkcel_PsrField *psr);

LIBPOPKCEL_EXTERN int popkcel_psrTryConnect(struct Popkcel_PsrField *psr);

LIBPOPKCEL_EXTERN int popkcel_psrTrySend(struct Popkcel_PsrField *psr, const char *data, size_t len, Popkcel_FuncCallback callback, void *userData);

/// 函数中会调用initPsrField，accept完之后如果要删除需要自行调用destroyPsrField
LIBPOPKCEL_EXTERN void popkcel_psrAcceptOne(struct Popkcel_PsrSocket *sock, struct Popkcel_PsrField *psr, Popkcel_PsrFuncCallback cbFunc);

/// @brief psr默认情况下会对要发送的数据进行缓存，等过了一个很短的时间后再把数据发出去，这和TCP协议是类似的。如果你希望不要缓存，要立即把数据发出去，那么就调用这个函数。
/// @param psr 要立即发送的psrField。
/// @return 返回正整数表示成功发送的字节数，返回POPKCEL_ERROR表示出错，返回POPKCEL_WOULDBLOCK表示该操作需要异步等待。
LIBPOPKCEL_EXTERN int popkcel_psrSendCache(struct Popkcel_PsrField *psr);

LIBPOPKCEL_EXTERN struct Popkcel_PsrField *popkcel_psrFind(struct Popkcel_PsrSocket *sock, struct sockaddr *addr);

#ifdef __cplusplus
}
#endif

#endif
