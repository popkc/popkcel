/*
Copyright (C) 2020 popkc(popkcer at gmail dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef POPKCEL_H
#define POPKCEL_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#    include <BaseTsd.h>
#    include <WinSock2.h>
#    include <ws2tcpip.h>

#    include <windows.h>
typedef SSIZE_T ssize_t;
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    ifdef __linux__
#        include <sys/epoll.h>
#    else
#        include <sys/time.h>
#        include <sys/types.h>

#        include <sys/event.h>
#    endif
#endif

#ifndef POPKCEL_SINGLETHREAD
#    include <pthread.h>
#endif

#ifndef LIBPOPKCEL_EXTERN
#    ifdef _WIN32
#        if defined(LIBPOPKCEL_SHARED)
#            define LIBPOPKCEL_EXTERN __declspec(dllexport)
#        elif defined(USING_LIBPOPKCEL_SHARED)
#            define LIBPOPKCEL_EXTERN __declspec(dllimport)
#        else
#            define LIBPOPKCEL_EXTERN
#        endif
#    elif __GNUC__ >= 4
#        define LIBPOPKCEL_EXTERN __attribute__((visibility("default")))
#    else
#        define LIBPOPKCEL_EXTERN
#    endif
#endif

#ifndef STACKGROWTHUP
#    define ELCHECKIFONSTACK2(l, v, s) assert(((char*)(v) < (char*)&sp || (char*)(v) > (char*)(l)->stackPos) && s)
#else
#    define ELCHECKIFONSTACK2(l, v, s) assert(((char*)(v) > (char*)&sp || (char*)(v) < (char*)(l)->stackPos) && s)
#endif

#ifdef NDEBUG
#    define ELCHECKIFONSTACK(l, v, s)
#else
#    define ELCHECKIFONSTACK(l, v, s) \
        volatile char sp;             \
        ELCHECKIFONSTACK2(l, v, s)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#    define POPKCEL_THREADLOCAL __declspec(thread)
#    ifdef _M_IX86
typedef int32_t PopkcJmpBuf[6];
#        define POPKCJMPBUF PopkcJmpBuf
#    else
typedef int64_t PopkcJmpBuf[30];
#        define POPKCJMPBUF PopkcJmpBuf
#    endif
LIBPOPKCEL_EXTERN int popkcSetjmp(PopkcJmpBuf env);
LIBPOPKCEL_EXTERN void popkcLongjmp(PopkcJmpBuf env, int value);
#    define POPKCSETJMP popkcSetjmp
#    define POPKCLONGJMP popkcLongjmp
#else
#    define POPKCSETJMP setjmp
#    define POPKCLONGJMP longjmp
#    define POPKCJMPBUF jmp_buf
#    define POPKCEL_THREADLOCAL __thread
#endif

typedef void (*Popkcel_FuncCallback)(void* data, intptr_t value);

#ifndef _WIN32
typedef int Popkcel_HandleType;
#    define POPKCEL_HANDLEFIELD            \
        struct Popkcel_SingleOperation so; \
        struct Popkcel_Loop* loop;         \
        Popkcel_HandleType fd;
#    define POPKCEL_SOCKETPF
#else
struct Popkcel_Socket;
struct Popkcel_IocpCallback
{
    OVERLAPPED ol;
    Popkcel_FuncCallback funcCb;
    void* cbData;
    struct Popkcel_Socket* sock;
    Popkcel_FuncCallback funcCb2;
    void* cbData2;
    struct Popkcel_IocpCallback* next;
};

typedef HANDLE Popkcel_HandleType;
#    define POPKCEL_HANDLEFIELD    \
        struct Popkcel_Loop* loop; \
        Popkcel_HandleType fd;

#    define POPKCEL_SOCKETPF             \
        struct Popkcel_IocpCallback* ic; \
        int af;
#endif

enum Popkcel_Rv {
    POPKCEL_OK = 0,
    POPKCEL_ERROR = -1,
    POPKCEL_WOULDBLOCK = -2,
    POPKCEL_PENDING = -3
};

enum Popkcel_Event {
    POPKCEL_EVENT_IN = 1,
    POPKCEL_EVENT_OUT = 2,
    POPKCEL_EVENT_ERROR = 4,
    POPKCEL_EVENT_EDGE = 8,
    POPKCEL_EVENT_USER = 16
};

enum Popkcel_SocketType {
    POPKCEL_SOCKETTYPE_TCP = 1,
    POPKCEL_SOCKETTYPE_UDP = 2,
    POPKCEL_SOCKETTYPE_IPV6 = 4,
    POPKCEL_SOCKETTYPE_EXIST = 8
};

struct Popkcel_Context
{
    POPKCJMPBUF jmpBuf;
    char* stackPos;
    char* savedStack;
};

static inline void popkcel_initContext(struct Popkcel_Context* context)
{
    context->savedStack = NULL;
}

LIBPOPKCEL_EXTERN void popkcel_destroyContext(struct Popkcel_Context* context);

struct Popkcel_Rbtnode
{
    struct Popkcel_Rbtnode *parent, *left, *right;
    int64_t key;
    void* value;
    char isRed;
};

struct Popkcel_RbtInsertPos
{
    struct Popkcel_Rbtnode** ipos;
    struct Popkcel_Rbtnode* parent;
};

LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtFind(struct Popkcel_Rbtnode* root, int64_t key);
LIBPOPKCEL_EXTERN struct Popkcel_RbtInsertPos popkcel_rbtInsertPos(struct Popkcel_Rbtnode** root, int64_t key);
LIBPOPKCEL_EXTERN void popkcel_rbtInsertAtPos(struct Popkcel_Rbtnode** root, struct Popkcel_RbtInsertPos ipos,
    struct Popkcel_Rbtnode* inode);
LIBPOPKCEL_EXTERN void popkcel_rbtMultiInsert(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* inode);
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtNext(struct Popkcel_Rbtnode* node);
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtBegin(struct Popkcel_Rbtnode* root);
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtDelete(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node);

struct Popkcel_SingleOperation
{
    Popkcel_FuncCallback inRedo;
    Popkcel_FuncCallback outRedo;
    Popkcel_FuncCallback inCb;
    Popkcel_FuncCallback outCb;
    struct Popkcel_SingleOperation* next;
    void *inRedoData, *outRedoData, *inCbData, *outCbData;
};

LIBPOPKCEL_EXTERN int popkcel_close(Popkcel_HandleType fd);

struct Popkcel_Loop;

struct Popkcel_Handle
{
    POPKCEL_HANDLEFIELD
};

LIBPOPKCEL_EXTERN void popkcel_initHandle(struct Popkcel_Handle* handle, struct Popkcel_Loop* loop);

struct Popkcel_Timer
{
    struct Popkcel_Loop* loop;
    struct Popkcel_Rbtnode* iter;
    Popkcel_FuncCallback funcCb;
    void* cbData;
    unsigned int interval;
};

LIBPOPKCEL_EXTERN void popkcel_initTimer(struct Popkcel_Timer* timer, struct Popkcel_Loop* loop);
LIBPOPKCEL_EXTERN void popkcel_setTimer(struct Popkcel_Timer* timer, unsigned int timeout, unsigned int interval);
LIBPOPKCEL_EXTERN void popkcel_stopTimer(struct Popkcel_Timer* timer);

struct Popkcel_SysTimer
{
    POPKCEL_HANDLEFIELD
    Popkcel_FuncCallback funcCb;
    void* cbData;
};

LIBPOPKCEL_EXTERN void popkcel_initSysTimer(struct Popkcel_SysTimer* sysTimer, struct Popkcel_Loop* loop);
LIBPOPKCEL_EXTERN void popkcel_destroySysTimer(struct Popkcel_SysTimer* sysTimer);
LIBPOPKCEL_EXTERN void popkcel_setSysTimer(struct Popkcel_SysTimer* sysTimer, unsigned int timeout, char periodic, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN void popkcel_stopSysTimer(struct Popkcel_SysTimer* sysTimer);
void popkcel__invokeLoop(void* data, intptr_t rv);

struct Popkcel_Notifier
{
    POPKCEL_HANDLEFIELD
#ifdef _WIN32
    Popkcel_FuncCallback funcCb;
    void* cbData;
#endif
};

LIBPOPKCEL_EXTERN void popkcel_initNotifier(struct Popkcel_Notifier* notifier, struct Popkcel_Loop* loop, Popkcel_HandleType fd);
LIBPOPKCEL_EXTERN void popkcel_destroyNotifier(struct Popkcel_Notifier* notifier);
LIBPOPKCEL_EXTERN void popkcel_notifierSetCb(struct Popkcel_Notifier* notifier, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN int popkcel_notifierNotify(struct Popkcel_Notifier* notifier);

#define POPKCEL_SOCKETFIELD \
    struct sockaddr* addr;  \
    struct sockaddr* raddr; \
    char* buf;              \
    char* rbuf;             \
    char* pos;              \
    size_t len;             \
    size_t rlen;            \
    socklen_t addrLen;      \
    socklen_t* raddrLen;    \
    char bufCreated;        \
    char addrCreated;       \
    POPKCEL_SOCKETPF

struct Popkcel_Socket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETFIELD
};

LIBPOPKCEL_EXTERN int popkcel_initSocket(struct Popkcel_Socket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd);
LIBPOPKCEL_EXTERN void popkcel_destroySocket(struct Popkcel_Socket* sock);
LIBPOPKCEL_EXTERN int popkcel_tryConnect(struct Popkcel_Socket* sock, struct sockaddr* addr, socklen_t len, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN ssize_t popkcel_tryWrite(struct Popkcel_Socket* sock, const char* buf, size_t len, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN ssize_t popkcel_trySendto(struct Popkcel_Socket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN ssize_t popkcel_tryRead(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data);
LIBPOPKCEL_EXTERN ssize_t popkcel_tryRecvfrom(struct Popkcel_Socket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb, void* data);

struct Popkcel_MultiOperation
{
    struct Popkcel_Context context;
    struct Popkcel_Timer timer;
    struct Popkcel_Rbtnode* rvs;
    struct Popkcel_Loop* loop;
    struct Popkcel_PSSocket* curSocket;
    int count;
    char multiCallback;
    char timeOuted;
};

LIBPOPKCEL_EXTERN void popkcel_initMultiOperation(struct Popkcel_MultiOperation*, struct Popkcel_Loop* loop);
LIBPOPKCEL_EXTERN void popkcel_destroyMultiOperation(struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN void popkcel_resetMultiOperation(struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN void popkcel_multiOperationWait(struct Popkcel_MultiOperation* mo, int timeout, char multiCallback);
LIBPOPKCEL_EXTERN void popkcel_multiOperationReblock(struct Popkcel_MultiOperation* mo);

#define POPKCEL_PSSOCKETFIELD          \
    struct Popkcel_MultiOperation* mo; \
    size_t totalRead;

struct Popkcel_PSSocket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETFIELD
    POPKCEL_PSSOCKETFIELD
};

static inline void popkcel_destroyPSSocket(struct Popkcel_PSSocket* sock)
{
    popkcel_destroySocket((struct Popkcel_Socket*)sock);
}

LIBPOPKCEL_EXTERN void popkcel_initPSSocket(struct Popkcel_PSSocket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd);
LIBPOPKCEL_EXTERN void popkcel_multiConnect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN int popkcel_connect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, int len, int timeout);
LIBPOPKCEL_EXTERN void popkcel_multiWrite(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN ssize_t popkcel_write(struct Popkcel_PSSocket* sock, const char* buf, size_t len, int timeout);
LIBPOPKCEL_EXTERN void popkcel_multiSendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN ssize_t popkcel_sendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, int timeout);
LIBPOPKCEL_EXTERN void popkcel_multiRead(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN ssize_t popkcel_read(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout);
LIBPOPKCEL_EXTERN void popkcel_multiReadFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN ssize_t popkcel_readFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout);
LIBPOPKCEL_EXTERN void popkcel_multiRecvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, struct Popkcel_MultiOperation* mo);
LIBPOPKCEL_EXTERN ssize_t popkcel_recvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, int timeout);

typedef void (*Popkcel_FuncAccept)(void* data, Popkcel_HandleType fd, struct sockaddr* addr, socklen_t addrLen);

struct Popkcel_Listener
{
    POPKCEL_HANDLEFIELD
    Popkcel_FuncAccept funcAccept;
    void* funcAcceptData;
#ifdef _WIN32
    char buffer[sizeof(struct sockaddr_in6) * 2 + 32];
    Popkcel_HandleType curSock;
#endif
    char ipv6;
};

LIBPOPKCEL_EXTERN void popkcel_initListener(struct Popkcel_Listener* listener, struct Popkcel_Loop* loop, char ipv6, Popkcel_HandleType fd);
LIBPOPKCEL_EXTERN void popkcel_destroyListener(struct Popkcel_Listener* listener);
LIBPOPKCEL_EXTERN int popkcel_listen(struct Popkcel_Listener* listener, uint16_t port, int backlog);

struct Popkcel_OneShot
{
    struct Popkcel_Notifier notifier;
    Popkcel_FuncCallback cb;
    void* data;
};

struct Popkcel_Loop
{
    POPKCJMPBUF jmpBuf;
    struct Popkcel_SysTimer sysTimer;
#ifndef _WIN32
    struct Popkcel_SingleOperation* soToDelete;
#    ifdef __linux__
    struct epoll_event* events;
#    else
    struct kevent* events;
#    endif
#else
    struct Popkcel_IocpCallback* curOverlapped;
    ULONG_PTR completionKey;
    DWORD numOfBytes;
#endif
    size_t maxEvents;
    struct Popkcel_Rbtnode* timers;
    char* stackPos;
    struct Popkcel_Context* curContext;
    //struct Popkcel_HashInfo** moHash;
    //size_t hashSize;
    Popkcel_HandleType loopFd;
    int numOfEvents;
    int curIndex;
    char inited;
    char running;
};

LIBPOPKCEL_EXTERN void popkcel_initLoop(struct Popkcel_Loop* loop, size_t maxEvents);
LIBPOPKCEL_EXTERN void popkcel_destroyLoop(struct Popkcel_Loop* loop);
int popkcel__checkTimers();
LIBPOPKCEL_EXTERN int64_t popkcel_getCurrentTime();
LIBPOPKCEL_EXTERN int popkcel_addHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle, int ev);
LIBPOPKCEL_EXTERN int popkcel_removeHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle);
LIBPOPKCEL_EXTERN void popkcel_oneShotCallback(struct Popkcel_Loop* loop, Popkcel_FuncCallback cb, void* data);

#ifdef POPKCEL_SINGLETHREAD
#    define Popkcel_ThreadType int
#else
#    define Popkcel_ThreadType pthread_t
#endif
struct Popkcel_LoopPool
{
    struct Popkcel_Loop* loops;
    Popkcel_ThreadType* threads;
    size_t loopSize;
    char isRun;
};

LIBPOPKCEL_EXTERN void popkcel_initLoopPool(struct Popkcel_LoopPool* loopPool, size_t loopSize, size_t maxEvents);
LIBPOPKCEL_EXTERN void popkcel_destroyLoopPool(struct Popkcel_LoopPool* loopPool);
LIBPOPKCEL_EXTERN void popkcel_loopPoolDetach(struct Popkcel_LoopPool* loopPool);
LIBPOPKCEL_EXTERN int popkcel_loopPoolRun(struct Popkcel_LoopPool* loopPool);
LIBPOPKCEL_EXTERN void popkcel_moveSocket(struct Popkcel_LoopPool* loopPool, size_t threadNum, struct Popkcel_Socket* sock);

extern POPKCEL_THREADLOCAL struct Popkcel_Loop* popkcel_threadLoop;

LIBPOPKCEL_EXTERN int popkcel_runLoop(struct Popkcel_Loop* loop);
LIBPOPKCEL_EXTERN int popkcel_init();
LIBPOPKCEL_EXTERN int popkcel_address(struct sockaddr_in* addr, const char* ip, uint16_t port);
LIBPOPKCEL_EXTERN int popkcel_address6(struct sockaddr_in6* addr, const char* ip, uint16_t port);
LIBPOPKCEL_EXTERN struct sockaddr_in popkcel_addressI(uint32_t ip, uint16_t port);
LIBPOPKCEL_EXTERN void popkcel_resume(struct Popkcel_Context* context);
LIBPOPKCEL_EXTERN void popkcel_suspend(struct Popkcel_Context* context);

#ifndef _WIN32
void popkcel__notifierInRedo(void* data, intptr_t ev);
void popkcel__clearSo(struct Popkcel_Loop* loop);
void popkcel__eventCall(struct Popkcel_SingleOperation* so, int event);
#endif

#ifdef __cplusplus
}
#endif

#endif
