/*
Copyright (C) 2020-2023 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "popkcel.h"

#include <MSWSock.h>
#include <assert.h>
#include <string.h>

struct GlobalVars
{
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;
};
static struct GlobalVars* globalVars;

void popkcel_initHandle(struct Popkcel_Handle* handle, struct Popkcel_Loop* loop)
{
    handle->loop = loop;
}

static inline void initIocpCallback(struct Popkcel_IocpCallback* iocp)
{
    memset(iocp, 0, sizeof(struct Popkcel_IocpCallback));
}

void popkcel_initLoop(struct Popkcel_Loop* loop, size_t maxEvents)
{
    loop->running = 0;
    loop->timers = NULL;
    loop->loopFd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    popkcel_initSysTimer(&loop->sysTimer, loop);
    loop->curOverlapped = NULL;
}

void popkcel_destroyLoop(struct Popkcel_Loop* loop)
{
    /*struct Popkcel_Rbtnode* it = popkcel_rbtBegin(loop->timers);
    while (it) {
        struct Popkcel_Rbtnode* it2 = it;
        it = popkcel_rbtNext(it);
        free(it2);
    }*/
    popkcel_destroySysTimer(&loop->sysTimer);
    if (loop->curOverlapped)
        free(loop->curOverlapped);
    CloseHandle(loop->loopFd);
}

int popkcel_runLoop(struct Popkcel_Loop* loop)
{
#ifndef POPKCEL_NOFAKESYNC
    volatile char stackPos;
    loop->stackPos = (char*)&stackPos;
#endif
    popkcel_threadLoop = loop;
    loop->running = 1;
#ifndef POPKCEL_NOFAKESYNC
    if (POPKCSETJMP(loop->jmpBuf)) {
        loop = popkcel_threadLoop;
    }
#endif

    while (loop->running) {
        int r = popkcel__checkTimers();
        struct Popkcel_IocpCallback* ol;
        r = GetQueuedCompletionStatus(loop->loopFd, &loop->numOfBytes, &loop->completionKey, (LPOVERLAPPED*)&ol, r == -1 ? INFINITE : r);
        if (ol) {
            if (loop->curOverlapped)
                free(loop->curOverlapped);
            loop->curOverlapped = ol;

            if (ol->sock) {
                struct Popkcel_IocpCallback** ic = &ol->sock->ic;
                while (*ic) {
                    if (*ic == ol) {
                        *ic = ol->next;
                        break;
                    }
                    ic = &(*ic)->next;
                }
            }

            if (ol->funcCb) {
                ol->funcCb(ol->cbData, r == FALSE ? POPKCEL_ERROR : POPKCEL_OK);
            }
            free(loop->curOverlapped);
            loop->curOverlapped = NULL;
        }
    }
    return 0;
}

int popkcel_addHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle, int ev)
{
    if (CreateIoCompletionPort(handle->fd, loop->loopFd, (ULONG_PTR)handle, 0))
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

int popkcel_removeHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle)
{
    return POPKCEL_ERROR;
}

void popkcel_initNotifier(struct Popkcel_Notifier* notifier, struct Popkcel_Loop* loop)
{
    // popkcel_initHandle((struct Popkcel_Handle*)notifier, loop);
    notifier->loop = loop;
}

static int notifierCb(void* data, intptr_t rv)
{
    struct Popkcel_Notifier* notifier = data;
    if (notifier->funcCb)
        notifier->funcCb(notifier->cbData, rv);
    return 0;
}

int popkcel_notifierNotify(struct Popkcel_Notifier* notifier)
{
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback));
    initIocpCallback(ic);
    ic->funcCb = &notifierCb;
    ic->cbData = notifier;
    if (PostQueuedCompletionStatus(notifier->loop->loopFd, 0, (ULONG_PTR)notifier, (LPOVERLAPPED)ic))
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

void popkcel_notifierSetCb(struct Popkcel_Notifier* notifier, Popkcel_FuncCallback cb, void* data)
{
    notifier->funcCb = cb;
    notifier->cbData = data;
}

void popkcel_destroyNotifier(struct Popkcel_Notifier* notifier)
{
    // destroyHandle((struct Popkcel_Handle*)notifier);
}

static VOID CALLBACK sysTimerCb(PVOID data, BOOLEAN fired)
{
    struct Popkcel_SysTimer* st = (struct Popkcel_SysTimer*)data;
    if (st->funcCb)
        st->funcCb(st->cbData, POPKCEL_OK);
}

void popkcel_initSysTimer(struct Popkcel_SysTimer* sysTimer, struct Popkcel_Loop* loop)
{
    sysTimer->loop = loop;
    sysTimer->fd = 0;
}

void popkcel_setSysTimer(struct Popkcel_SysTimer* sysTimer, unsigned int timeout, char periodic, Popkcel_FuncCallback cb, void* data)
{
    popkcel_stopSysTimer(sysTimer);
    sysTimer->funcCb = cb;
    sysTimer->cbData = data;
    CreateTimerQueueTimer(&sysTimer->fd, NULL, sysTimerCb, sysTimer, timeout, periodic ? timeout : 0, WT_EXECUTEINTIMERTHREAD);
}

void popkcel_stopSysTimer(struct Popkcel_SysTimer* sysTimer)
{
    if (sysTimer->fd) {
        DeleteTimerQueueTimer(NULL, sysTimer->fd, NULL);
        sysTimer->fd = 0;
    }
}

void popkcel_destroySysTimer(struct Popkcel_SysTimer* sysTimer)
{
    popkcel_stopSysTimer(sysTimer);
}

int popkcel_init()
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd))
        return 1;
    globalVars = malloc(sizeof(struct GlobalVars));
    DWORD numBytes;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    {
        GUID guid = WSAID_CONNECTEX;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->ConnectEx, sizeof(globalVars->ConnectEx), &numBytes, NULL, NULL))
            return 2;
    }
    {
        GUID guid = WSAID_ACCEPTEX;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->AcceptEx, sizeof(globalVars->AcceptEx), &numBytes, NULL, NULL))
            return 3;
    }
    {
        GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &globalVars->GetAcceptExSockaddrs, sizeof(globalVars->GetAcceptExSockaddrs), &numBytes, NULL, NULL))
            return 4;
    }
    closesocket(s);
    popkcel__globalInit();
    return 0;
}

int popkcel_close(Popkcel_HandleType fd)
{
    if (CloseHandle(fd) == TRUE)
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

int popkcel_closeSocket(Popkcel_HandleType fd)
{
    if (!closesocket((SOCKET)fd))
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

int popkcel_initSocket(struct Popkcel_Socket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd)
{
    sock->loop = loop;
    sock->ic = NULL;
    sock->ipv6 = (socketType & POPKCEL_SOCKETTYPE_IPV6) ? 1 : 0;
    if (socketType & POPKCEL_SOCKETTYPE_EXIST) {
        sock->fd = fd;
    }
    else {
        sock->fd = (Popkcel_HandleType)WSASocketW(sock->ipv6 ? AF_INET6 : AF_INET,
            (socketType & POPKCEL_SOCKETTYPE_TCP) ? SOCK_STREAM : SOCK_DGRAM,
            0, NULL, 0, WSA_FLAG_OVERLAPPED);
    }
    return popkcel_addHandle(loop, (struct Popkcel_Handle*)sock, 0);
}

void popkcel_destroySocket(struct Popkcel_Socket* sock)
{
    struct Popkcel_IocpCallback* ic = sock->ic;
    while (ic) {
        ic->funcCb = NULL;
        ic->sock = NULL;
        ic = ic->next;
    }

    if (sock->fd)
        closesocket((SOCKET)sock->fd);
}

static int overlappedCommonCb(void* data, intptr_t rv)
{
    struct Popkcel_IocpCallback* ic = data;
    struct Popkcel_IocpCallback** ic2 = &ic->sock->ic;
    while (*ic2) {
        if (*ic2 == ic) {
            *ic2 = ic->next;
            break;
        }
        ic2 = &(*ic2)->next;
    }

    if (ic->funcCb2)
        ic->funcCb2(ic->cbData2, rv < 0 ? POPKCEL_ERROR : (ssize_t)popkcel_threadLoop->numOfBytes);
    return 0;
}

static ssize_t setOl(ssize_t r, Popkcel_FuncCallback cb, void* data, struct Popkcel_IocpCallback* ic, struct Popkcel_Socket* so)
{
    ssize_t rv;
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == ERROR_IO_PENDING) {
            ic->funcCb = &overlappedCommonCb;
            ic->cbData = ic;
            ic->funcCb2 = cb;
            ic->cbData2 = data;
            rv = POPKCEL_WOULDBLOCK;
        }
        else {
            free(ic);
            return POPKCEL_ERROR;
        }
    }
    else
        rv = r;
    ic->sock = so;
    ic->next = so->ic;
    so->ic = ic;
    return rv;
}

int popkcel_tryConnect(struct Popkcel_Socket* sock, struct sockaddr* addr, socklen_t len, Popkcel_FuncCallback cb, void* data)
{
    if (sock->ipv6) {
        struct sockaddr_in6 sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        bind((SOCKET)sock->fd, (struct sockaddr*)&sa, sizeof(sa));
    }
    else {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        bind((SOCKET)sock->fd, (struct sockaddr*)&sa, sizeof(sa));
    }
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback));
    initIocpCallback(ic);
    BOOL r = globalVars->ConnectEx((SOCKET)sock->fd, addr, len, NULL, 0, NULL, (LPOVERLAPPED)ic);
    return (int)setOl(r == TRUE ? POPKCEL_OK : -1, cb, data, ic, sock);
}

ssize_t popkcel_tryWrite(struct Popkcel_Socket* sock, const char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback) + len);
    char* nbuf = (char*)ic + sizeof(struct Popkcel_IocpCallback);
    memcpy(nbuf, buf, len);
    initIocpCallback(ic);
    DWORD bw;
    BOOL r = WriteFile(sock->fd, nbuf, (DWORD)len, &bw, (LPOVERLAPPED)ic);
    return setOl(r >= 0 ? (ssize_t)bw : -1, cb, data, ic, sock);
}

ssize_t popkcel_trySendto(struct Popkcel_Socket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data)
{
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback) + sizeof(WSABUF) + len);
    initIocpCallback(ic);
    WSABUF* wb = (WSABUF*)((char*)ic + sizeof(struct Popkcel_IocpCallback));
    wb->buf = (char*)wb + sizeof(WSABUF);
    wb->len = (ULONG)len;
    memcpy(wb->buf, buf, len);
    DWORD bw;
    int r = WSASendTo((SOCKET)sock->fd, wb, 1, &bw, 0, addr, addrLen, (LPOVERLAPPED)ic, NULL);
    return setOl(r == 0 ? (ssize_t)bw : -1, cb, data, ic, sock);
}

ssize_t popkcel_tryRead(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback));
    initIocpCallback(ic);
    DWORD br;
    BOOL r = ReadFile(sock->fd, buf, (DWORD)len, &br, (LPOVERLAPPED)ic);
    return setOl(r == TRUE ? (ssize_t)br : -1, cb, data, ic, sock);
}

ssize_t popkcel_tryReadFor(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback));
    initIocpCallback(ic);
    DWORD br;
    BOOL r = ReadFile(sock->fd, buf, (DWORD)len, &br, (LPOVERLAPPED)ic);
    return setOl(r == TRUE ? (ssize_t)br : -1, cb, data, ic, sock);
}

struct RFS
{
    struct Popkcel_IocpCallback ic;
    WSABUF wb;
    DWORD flag;
};

ssize_t popkcel_tryRecvfrom(struct Popkcel_Socket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    ELCHECKIFONSTACK2(sock->loop, addr, "Do not allocate addr on stack!");
    ELCHECKIFONSTACK2(sock->loop, addrLen, "Do not allocate addrLen on stack!");
    struct RFS* rfs = malloc(sizeof(struct RFS));
    initIocpCallback(&rfs->ic);
    rfs->wb.buf = buf;
    rfs->wb.len = (ULONG)len;
    rfs->flag = 0;
    DWORD br;
    int r = WSARecvFrom((SOCKET)sock->fd, &rfs->wb, 1, &br, &rfs->flag, addr, addrLen, (LPOVERLAPPED)rfs, NULL);
    return setOl(r == 0 ? (ssize_t)br : -1, cb, data, &rfs->ic, sock);
}

void popkcel_destroyListener(struct Popkcel_Listener* listener)
{
    closesocket((SOCKET)listener->curSock);
    closesocket((SOCKET)listener->fd);
}

static void listenerAcceptOne(struct Popkcel_Listener* listener);

static int acceptCb(void* data, intptr_t rv)
{
    struct Popkcel_Listener* listener = data;
    Popkcel_HandleType olds = listener->curSock;
    struct sockaddr *la, *ra;
    socklen_t li, ri;
    DWORD addrLen;
    if (listener->ipv6)
        addrLen = sizeof(struct sockaddr_in6) + 16;
    else
        addrLen = sizeof(struct sockaddr_in) + 16;
    globalVars->GetAcceptExSockaddrs(listener->buffer, 0, addrLen, addrLen, &la, &li, &ra, &ri);
    listener->curSock = (Popkcel_HandleType)WSASocketW(listener->ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    listenerAcceptOne(listener);
    listener->funcAccept(listener->funcAcceptData, olds, ra, ri);
    return 0;
}

static void listenerAcceptOne(struct Popkcel_Listener* listener)
{
    struct Popkcel_IocpCallback* ic = malloc(sizeof(struct Popkcel_IocpCallback));
    initIocpCallback(ic);
    ic->funcCb = &acceptCb;
    ic->cbData = listener;
    DWORD addrLen;
    if (listener->ipv6)
        addrLen = sizeof(struct sockaddr_in6) + 16;
    else
        addrLen = sizeof(struct sockaddr_in) + 16;
    globalVars->AcceptEx((SOCKET)listener->fd, (SOCKET)listener->curSock, listener->buffer, 0, addrLen, addrLen, NULL, (LPOVERLAPPED)ic);
}

void popkcel_initListener(struct Popkcel_Listener* listener, struct Popkcel_Loop* loop, char ipv6, Popkcel_HandleType fd)
{
    listener->loop = loop;
    listener->ipv6 = ipv6;
    if (fd)
        listener->fd = fd;
    else
        listener->fd = (Popkcel_HandleType)WSASocketW(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    popkcel_addHandle(loop, (struct Popkcel_Handle*)listener, 0);
    listener->curSock = (Popkcel_HandleType)WSASocketW(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
}

int popkcel_listen(struct Popkcel_Listener* listener, uint16_t port, int backlog)
{
    popkcel_bind((struct Popkcel_Socket*)listener, port);
    if (listen((SOCKET)listener->fd, backlog))
        return POPKCEL_ERROR;
    listenerAcceptOne(listener);
    return POPKCEL_OK;
}

int popkcel__invokeLoop(void* data, intptr_t rv)
{
    struct Popkcel_Loop* loop = data;
    PostQueuedCompletionStatus(loop->loopFd, 0, 0, NULL);
    return 0;
}

void* popkcel_dlopen(const char* fileName)
{
    auto h = LoadLibrary(fileName);
    return (void*)h;
}

int popkcel_dlclose(void* handle)
{
    if (FreeLibrary((HMODULE)handle))
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

void* popkcel_dlsym(void* handle, const char* symbol)
{
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}

int64_t popkcel_getCurrentTime()
{
#if (_WIN32_WINNT >= 0x0600)
    return GetTickCount64();
#else
    return GetTickCount();
#endif
}
