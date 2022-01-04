/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef POPKCEL_HPP
#define POPKCEL_HPP

#include "popkcel.h"

namespace popkcel {

typedef Popkcel_HandleType HandleType;
typedef Popkcel_FuncCallback FuncCallback;

enum Rv {
    RV_OK = POPKCEL_OK,
    RV_ERROR = POPKCEL_ERROR,
    RV_WOULDBLOCK = POPKCEL_WOULDBLOCK,
    RV_PENDING = POPKCEL_PENDING
};

enum Event {
    EVENT_IN = POPKCEL_EVENT_IN,
    EVENT_OUT = POPKCEL_EVENT_OUT,
    EVENT_ERROR = POPKCEL_EVENT_ERROR,
    EVENT_EDGE = POPKCEL_EVENT_EDGE
};

enum SocketType {
    SOCKETTYPE_TCP = POPKCEL_SOCKETTYPE_TCP,
    SOCKETTYPE_UDP = POPKCEL_SOCKETTYPE_UDP,
    SOCKETTYPE_IPV6 = POPKCEL_SOCKETTYPE_IPV6,
    SOCKETTYPE_EXIST = POPKCEL_SOCKETTYPE_EXIST
};

#define POPKCEL_LOOPFUNC \
    Loop* getLoop() { return reinterpret_cast<Loop*>(loop); }

struct Context : Popkcel_Context
{
    Context()
    {
        popkcel_initContext(this);
    }

    ~Context()
    {
        popkcel_destroyContext(this);
    }

    void suspend()
    {
        popkcel_suspend(this);
    }

    void resume()
    {
        popkcel_resume(this);
    }
};

struct SingleOperation : Popkcel_SingleOperation
{
};

inline void closeFd(HandleType fd)
{
    popkcel_close(fd);
}

struct Loop;

struct Handle : Popkcel_Handle
{
    POPKCEL_LOOPFUNC
};

struct Timer : Popkcel_Timer
{
    Timer(Loop* loop)
    {
        popkcel_initTimer(this, (Popkcel_Loop*)loop);
    }

    void start(unsigned int timeout, unsigned int interval = 0)
    {
        popkcel_setTimer(this, timeout, interval);
    }

    void stop()
    {
        popkcel_stopTimer(this);
    }

    ~Timer()
    {
        stop();
    }
    POPKCEL_LOOPFUNC
};

struct SysTimer : Popkcel_SysTimer
{
    SysTimer(Loop* loop)
    {
        popkcel_initSysTimer(this, (Popkcel_Loop*)loop);
    }

    ~SysTimer()
    {
        popkcel_destroySysTimer(this);
    }

    void start(unsigned int timeout, FuncCallback cb, void* data = NULL, char periodic = 0)
    {
        popkcel_setSysTimer(this, timeout, periodic, cb, data);
    }

    void stop()
    {
        popkcel_stopSysTimer(this);
    }
};

struct Notifier : Popkcel_Notifier
{
    Notifier(Loop* loop)
    {
        popkcel_initNotifier(this, (Popkcel_Loop*)loop);
    }

    ~Notifier()
    {
        popkcel_destroyNotifier(this);
    }

    void setCb(FuncCallback cb, void* data = NULL)
    {
        popkcel_notifierSetCb(this, cb, data);
    }

    void notify()
    {
        popkcel_notifierNotify(this);
    }
    POPKCEL_LOOPFUNC
};

struct Socket : Popkcel_Socket
{
    Socket() { }

    Socket(Loop* loop, int socketType = SOCKETTYPE_TCP, HandleType fd = 0)
    {
        popkcel_initSocket(this, (Popkcel_Loop*)loop, socketType, fd);
    }

    ~Socket()
    {
        popkcel_destroySocket(this);
    }

    int bind(uint16_t port)
    {
        return popkcel_bind(this, port);
    }

    int tryConnect(struct sockaddr* addr, socklen_t len, Popkcel_FuncCallback cb = NULL, void* data = NULL)
    {
        return popkcel_tryConnect(this, addr, len, cb, data);
    }

    ssize_t tryWrite(const char* buf, size_t len, Popkcel_FuncCallback cb = NULL, void* data = NULL)
    {
        return popkcel_tryWrite(this, buf, len, cb, data);
    }

    ssize_t trySendto(const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb = NULL, void* data = NULL)
    {
        return popkcel_trySendto(this, buf, len, addr, addrLen, cb, data);
    }

    ssize_t tryRead(char* buf, size_t len, Popkcel_FuncCallback cb = NULL, void* data = NULL)
    {
        return popkcel_tryRead(this, buf, len, cb, data);
    }

    ssize_t tryRecvfrom(char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb = NULL, void* data = NULL)
    {
        return popkcel_tryRecvfrom(this, buf, len, addr, addrLen, cb, data);
    }
    POPKCEL_LOOPFUNC
};

struct PSSocket;

struct MultiOperation : Popkcel_MultiOperation
{
    MultiOperation(Loop* loop)
    {
        popkcel_initMultiOperation(this, (Popkcel_Loop*)loop);
    }

    void reset()
    {
        popkcel_resetMultiOperation(this);
    }

    ~MultiOperation()
    {
        popkcel_destroyMultiOperation(this);
    }

    void wait(int timeout = 0, char multiCallback = 0)
    {
        popkcel_multiOperationWait(this, timeout, multiCallback);
    }

    void reblock()
    {
        popkcel_multiOperationReblock(this);
    }

    intptr_t getResult(PSSocket* sock)
    {
        return popkcel_multiOperationGetResult(this, (Popkcel_PSSocket*)sock);
    }
    POPKCEL_LOOPFUNC
};

struct PSSocket : Socket
{
    POPKCEL_PSSOCKETFIELD
    PSSocket(Loop* loop, int socketType = SOCKETTYPE_TCP, HandleType fd = 0)
    {
        popkcel_initPSSocket((Popkcel_PSSocket*)this, (Popkcel_Loop*)loop, socketType, fd);
    }

    ~PSSocket()
    {
        popkcel_destroyPSSocket((Popkcel_PSSocket*)this);
    }

    void multiConnect(struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiConnect((Popkcel_PSSocket*)this, addr, addrLen, mo);
    }

    int connect(struct sockaddr* addr, int len, int timeout = 0)
    {
        return popkcel_connect((Popkcel_PSSocket*)this, addr, len, timeout);
    }

    void multiWrite(const char* buf, size_t len, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiWrite((Popkcel_PSSocket*)this, buf, len, mo);
    }

    ssize_t write(const char* buf, size_t len, int timeout = 0)
    {
        return popkcel_write((Popkcel_PSSocket*)this, buf, len, timeout);
    }

    void multiSendto(const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiSendto((Popkcel_PSSocket*)this, buf, len, addr, addrLen, mo);
    }

    ssize_t sendto(const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, int timeout = 0)
    {
        return popkcel_sendto((Popkcel_PSSocket*)this, buf, len, addr, addrLen, timeout);
    }

    void multiRead(char* buf, size_t len, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiRead((Popkcel_PSSocket*)this, buf, len, mo);
    }

    ssize_t read(char* buf, size_t len, int timeout = 0)
    {
        return popkcel_read((Popkcel_PSSocket*)this, buf, len, timeout);
    }

    void multiReadFor(char* buf, size_t len, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiRead((Popkcel_PSSocket*)this, buf, len, mo);
    }

    ssize_t readFor(char* buf, size_t len, int timeout = 0)
    {
        return popkcel_readFor((Popkcel_PSSocket*)this, buf, len, timeout);
    }

    void multiRecvfrom(char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, struct Popkcel_MultiOperation* mo)
    {
        popkcel_multiRecvfrom((Popkcel_PSSocket*)this, buf, len, addr, addrLen, mo);
    }

    ssize_t recvfrom(char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, int timeout = 0)
    {
        return popkcel_recvfrom((Popkcel_PSSocket*)this, buf, len, addr, addrLen, timeout);
    }
};

struct Listener : Popkcel_Listener
{
    Listener(Loop* loop, char ipv6 = 0, HandleType fd = 0)
    {
        popkcel_initListener(this, (Popkcel_Loop*)loop, ipv6, fd);
    }

    ~Listener()
    {
        popkcel_destroyListener(this);
    }

    int listen(uint16_t port, int backlog = SOMAXCONN)
    {
        return popkcel_listen(this, port, backlog);
    }
    POPKCEL_LOOPFUNC
};

struct Loop : Popkcel_Loop
{
    Loop(size_t maxEvents = 0)
    {
        popkcel_initLoop(this, maxEvents);
    }

    ~Loop()
    {
        popkcel_destroyLoop(this);
    }

    void addHandle(Handle* handle, int ev = 0)
    {
        popkcel_addHandle(this, handle, ev);
    }

    void removeHandle(Handle* handle)
    {
        popkcel_removeHandle(this, handle);
    }

    void oneShotCallback(Popkcel_FuncCallback cb, void* data = NULL)
    {
        popkcel_oneShotCallback(this, cb, data);
    }

    int run()
    {
        return popkcel_runLoop(this);
    }
};

struct LoopPool : Popkcel_LoopPool
{
    LoopPool(size_t loopSize = 0, size_t maxEvents = 0)
    {
        popkcel_initLoopPool(this, loopSize, maxEvents);
    }

    ~LoopPool()
    {
        popkcel_destroyLoopPool(this);
    }

    void detach()
    {
        popkcel_loopPoolDetach(this);
    }

    int run()
    {
        return popkcel_loopPoolRun(this);
    }

    void moveSocket(size_t threadNum, struct Popkcel_Socket* sock)
    {
        popkcel_moveSocket(this, threadNum, sock);
    }

    Loop* getLoops() { return static_cast<Loop*>(loops); }
};

inline int init()
{
    return popkcel_init();
}

inline int address(struct sockaddr_in* addr, const char* ip, uint16_t port)
{
    return popkcel_address(addr, ip, port);
}

inline int address(struct sockaddr_in6* addr, const char* ip, uint16_t port)
{
    return popkcel_address6(addr, ip, port);
}

inline struct sockaddr_in address(uint32_t ip, uint16_t port)
{
    return popkcel_addressI(ip, port);
}

inline Loop* threadLoop()
{
    return static_cast<Loop*>(popkcel_threadLoop);
}

}

#endif
