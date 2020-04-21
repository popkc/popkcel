/*
Copyright (C) 2020 popkc(popkcer at gmail dot com)
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3 as published by
the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "popkcel.h"

#include <MSWSock.h>
#include <Windows.h>
#include <assert.h>
#include <string.h>

namespace popkcel {
//from https://stackoverflow.com/a/17387176
std::string GetLastErrorAsString()
{
    //Get the error message, if any.
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0)
        return std::string(); //No error message has been recorded

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    //Free the buffer.
    LocalFree(messageBuffer);

    return message;
}

struct GlobalVars
{
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;
};
GlobalVars* globalVars;

IocpCallback::IocpCallback()
{
    memset(&ol, 0, sizeof(ol));
}

Loop::Loop(LoopPool* loopPool, size_t maxEvents)
{
    this->loopPool = loopPool;
    if (loopPool) {
        loopFd = loopPool->iocp;
    }
    else {
        loopFd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    }
    auto s = GetLastErrorAsString();
    setupTimer();
}

Loop::Loop(const Loop& loop)
{
    this->loopPool = loopPool;
    this->loopFd = loop.loopFd;
    setupTimer();
}

Loop::~Loop()
{
    delete sysTimer;
    if (curOverlapped)
        delete curOverlapped;
    if (!loopPool) {
        CloseHandle(loopFd);
    }
}

void Loop::setTimeout(uint32_t timeout)
{
    sysTimer->setTimer(timeout, [this](intptr_t) {
        PostQueuedCompletionStatus(this->loopFd, 0, 0, NULL);
    });
}

int runLoop(Loop* loop)
{
    volatile char stackPos;
    loop->stackPos = (char*)&stackPos;
    threadLoop = loop;
    loop->running = true;
    if (POPKCSETJMP(loop->jmpBuf)) {
        loop = threadLoop;
    }
    do {
        int r = loop->checkTimers();
        OVERLAPPED* ol;
        r = GetQueuedCompletionStatus(loop->loopFd, &loop->numOfBytes, &loop->completionKey, &ol, r == -1 ? INFINITE : r);
        if (ol) {
            if (loop->curOverlapped)
                delete loop->curOverlapped;
            loop->curOverlapped = reinterpret_cast<IocpCallback*>(ol);
            if (loop->curOverlapped->funcCb) {
                loop->curOverlapped->funcCb(r == FALSE ? SOCKRV_ERROR : SOCKRV_OK);
            }
            delete loop->curOverlapped;
            loop->curOverlapped = nullptr;
        }
    } while (loop->running);
    return 0;
}

Notifier::Notifier(Loop* loop, HandleType fd)
    : Handle(loop)
{
    this->fd = 0;
}

int Notifier::notify()
{
    IocpCallback* ic = new IocpCallback;
    ic->funcCb = [this](intptr_t rv) {
        this->funcCb(rv);
    };
    if (PostQueuedCompletionStatus(loop->loopFd, 0, (ULONG_PTR)this, (LPOVERLAPPED)ic))
        return SOCKRV_OK;
    else
        return SOCKRV_ERROR;
}

void Notifier::setCb(const FuncCallback& cb)
{
    this->funcCb = cb;
}

Notifier::~Notifier() {}

static VOID CALLBACK sysTimerCb(PVOID data, BOOLEAN fired)
{
    SysTimer* st = reinterpret_cast<SysTimer*>(data);
    st->funcCb(SOCKRV_OK);
}

SysTimer::SysTimer(Loop* loop)
    : Handle(loop)
{
    fd = 0;
}

void SysTimer::setTimer(int timeout, const FuncCallback& cb, bool periodic)
{
    stopTimer();
    this->funcCb = cb;
    CreateTimerQueueTimer(&fd, NULL, sysTimerCb, this, timeout, periodic ? timeout : 0, WT_EXECUTEINTIMERTHREAD);
}

void SysTimer::stopTimer()
{
    if (fd) {
        DeleteTimerQueueTimer(NULL, fd, NULL);
        fd = 0;
    }
}

SysTimer::~SysTimer()
{
    stopTimer();
}

int init()
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd))
        return 1;
    globalVars = new GlobalVars;
    DWORD numBytes;
    GUID guid = WSAID_CONNECTEX;
    auto s = socket(AF_INET, SOCK_STREAM, 0);
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->ConnectEx, sizeof(globalVars->ConnectEx), &numBytes, NULL, NULL))
        return 2;
    guid = WSAID_ACCEPTEX;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->AcceptEx, sizeof(globalVars->AcceptEx), &numBytes, NULL, NULL))
        return 3;
    guid = WSAID_GETACCEPTEXSOCKADDRS;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->GetAcceptExSockaddrs, sizeof(globalVars->GetAcceptExSockaddrs), &numBytes, NULL, NULL))
        return 4;
    closesocket(s);
    return 0;
}

int closeFd(HandleType fd)
{
    return CloseHandle(fd);
}

Socket::Socket(Loop* loop, SocketType type)
    : Handle(loop)
{
    fd = (HandleType)WSASocketW(AF_INET, type == SOCKETTYPE_TCP ? SOCK_STREAM : SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    CreateIoCompletionPort(fd, loop->loopFd, (ULONG_PTR)this, 0);
}

Socket::Socket(Loop* loop, HandleType fd)
    : Handle(loop)
{
    this->fd = fd;
    CreateIoCompletionPort(fd, loop->loopFd, (ULONG_PTR)this, 0);
}

Socket::~Socket()
{
    if (ic) {
        ic->funcCb = 0;
    }
    if (fd) {
        closesocket((SOCKET)fd);
        fd = 0;
    }
}

static inline int setOl(int r, const FuncCallback& cb, Socket* so)
{
    if (r >= 0)
        return r;
    else {
        IocpCallback* ic = so->ic;
        int err = WSAGetLastError();
        if (err == ERROR_IO_PENDING) {
            //本库目标c++版本为c++11，所以不能用c++14的ic=so->ic这种写法
            ic->funcCb = [ic, so, cb](intptr_t rv) {
                if (so->ic == ic)
                    so->ic = nullptr;
                cb(rv);
            };
            return SOCKRV_WOULDBLOCK;
        }
        else {
            delete ic;
            so->ic = nullptr;
            return SOCKRV_ERROR;
        }
    }
}

int Socket::tryConnect(sockaddr* addr, socklen_t len, const FuncCallback& cb)
{
    auto baddr = address((uint32_t)INADDR_ANY, 0);
    ::bind((SOCKET)fd, (sockaddr*)&baddr, sizeof(baddr));
    ic = new IocpCallback;
    BOOL r = globalVars->ConnectEx((SOCKET)fd, addr, len, NULL, 0, NULL, (LPOVERLAPPED)ic);
    return setOl(r == TRUE ? SOCKRV_OK : -1, cb, this);
}

ssize_t Socket::tryWrite(const char* buf, size_t len, const FuncCallback& cb)
{
    char* nbuf = new char[len];
    memcpy(nbuf, buf, len);
    IocpCallback* ic = new IocpCallback;
    this->ic = ic;
    DWORD bw;
    BOOL r = WriteFile(fd, nbuf, (DWORD)len, &bw, (LPOVERLAPPED)ic);
    if (r == FALSE && WSAGetLastError() == ERROR_IO_PENDING) {
        ic->funcCb = [this, ic, cb, nbuf](intptr_t rv) {
            if (this->ic == ic)
                this->ic = nullptr;
            delete[] nbuf;
            cb(rv);
        };
        return SOCKRV_WOULDBLOCK;
    }
    else {
        delete[] nbuf;
        if (r == TRUE)
            return bw;
        else {
            delete ic;
            this->ic = nullptr;
            return SOCKRV_ERROR;
        }
    }
}

ssize_t Socket::trySendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, const FuncCallback& cb)
{
    IocpCallback* ic = new IocpCallback;
    this->ic = ic;
    WSABUF* wb = (WSABUF*)malloc(sizeof(WSABUF) + len);
    wb->buf = (char*)wb + sizeof(WSABUF);
    wb->len = (ULONG)len;
    memcpy(wb->buf, buf, len);
    DWORD bw;
    int r = WSASendTo((SOCKET)fd, wb, 1, &bw, 0, addr, addrLen, (LPOVERLAPPED)ic, NULL);
    if (r == 0 && WSAGetLastError() == ERROR_IO_PENDING) {
        ic->funcCb = [this, ic, cb, wb](intptr_t rv) {
            if (this->ic == ic)
                this->ic = nullptr;
            free(wb);
            cb(rv);
        };
        return SOCKRV_WOULDBLOCK;
    }
    else {
        free(wb);
        if (r)
            return bw;
        else {
            delete ic;
            this->ic = nullptr;
            return SOCKRV_ERROR;
        }
    }
}

ssize_t Socket::tryRead(char* buf, size_t len, const FuncCallback& cb)
{
    ELCHECKIFONSTACK(buf, "Do not allocate buffer on stack!");
    ic = new IocpCallback;
    DWORD br;
    BOOL r = ReadFile(fd, buf, (DWORD)len, &br, (LPOVERLAPPED)ic);
    return setOl(r == TRUE ? br : -1, cb, this);
}

struct RFS
{
    IocpCallback ic;
    WSABUF wb;
    DWORD flag;
};

ssize_t Socket::tryRecvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, const FuncCallback& cb)
{
    ELCHECKIFONSTACK(buf, "Do not allocate buffer on stack!");
    CHECKIFONSTACK(addr, "Do not allocate addr on stack!");
    CHECKIFONSTACK(addrLen, "Do not allocate addrLen on stack!");
    RFS* rfs = new RFS;
    rfs->wb.buf = buf;
    rfs->wb.len = (ULONG)len;
    this->ic = &rfs->ic;
    DWORD br;
    int r = WSARecvFrom((SOCKET)fd, &rfs->wb, 1, &br, &rfs->flag, addr, addrLen, (LPOVERLAPPED)rfs, NULL);
    return setOl(r == 0 ? br : -1, cb, this);
}

Handle::~Handle()
{
    if (fd) {
        CloseHandle(fd);
    }
}

Listener::~Listener()
{
    closesocket((SOCKET)curSock);
    closesocket((SOCKET)fd);
    fd = 0;
}

void Listener::acceptCb(intptr_t rv)
{
    HandleType olds = this->curSock;
    sockaddr *la, *ra;
    socklen_t li, ri;
    globalVars->GetAcceptExSockaddrs(buffer, 0, sizeof(sockaddr), sizeof(sockaddr), &la, &li, &ra, &ri);
    this->curSock = (HandleType)WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    this->acceptOne();
    this->funcAccept(olds, ra, ri);
}

void Listener::acceptOne()
{
    IocpCallback* ic = new IocpCallback;
    ic->funcCb = std::bind(&Listener::acceptCb, this, std::placeholders::_1);
    globalVars->AcceptEx((SOCKET)fd, (SOCKET)curSock, buffer, 0, sizeof(sockaddr) + 16, sizeof(sockaddr) + 16, NULL, (LPOVERLAPPED)ic);
}

Listener::Listener(Loop* loop, HandleType fd)
    : Handle(loop)
{
    if (fd)
        this->fd = fd;
    else
        this->fd = (HandleType)WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    CreateIoCompletionPort(this->fd, loop->loopFd, (ULONG_PTR)this, 0);
    curSock = (HandleType)WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
}

int Listener::listen(uint16_t port, int backlog)
{
    auto addr = address((uint32_t)0, port);
    if (::bind((SOCKET)fd, (sockaddr*)&addr, sizeof(addr)))
        return SOCKRV_ERROR;
    if (::listen((SOCKET)fd, backlog))
        return SOCKRV_ERROR;
    acceptOne();
    return SOCKRV_OK;
}

void LoopPool::moveSocket(size_t threadNum, Socket* socket)
{
}

LoopPool::LoopPool(size_t numOfLoops, size_t maxEvents)
    : iocp(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1))
    , loops(1, Loop(this, maxEvents))
{
}

}