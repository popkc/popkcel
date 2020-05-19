/*
Copyright (C) 2020 popkc(popkcer at gmail dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include "popkcel.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int popkcel_init()
{
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

void popkcel__eventCall(struct Popkcel_SingleOperation* so, int event)
{
    if (event & POPKCEL_EVENT_ERROR) {
        if (so->inRedo)
            so->inRedo(so->inRedoData, event);
        if (so->outRedo)
            so->outRedo(so->outRedoData, event);
    }
    else {
        if ((event & POPKCEL_EVENT_IN) && so->inRedo)
            so->inRedo(so->inRedoData, event);
        if ((event & POPKCEL_EVENT_OUT) && so->outRedo)
            so->outRedo(so->outRedoData, event);
    }
}

int64_t popkcel_getCurrentTime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int popkcel_close(Popkcel_HandleType fd)
{
    return close(fd);
}

void popkcel__clearSo(struct Popkcel_Loop* loop)
{
    if (!loop->soToDelete)
        return;
    struct Popkcel_SingleOperation *so, *so2;
    so2 = loop->soToDelete;
    do {
        so = so2;
        so2 = (struct Popkcel_SingleOperation*)so->next;
        free(so);
    } while (so2);
    loop->soToDelete = NULL;
}

void popkcel__notifierInRedo(void* data, intptr_t ev)
{
    if (ev & POPKCEL_EVENT_IN) {
        struct Popkcel_Notifier* nt = data;
        if (!nt->so.inCb)
            return;
        char buf[8];
        int r = read(nt->fd, buf, 8);
        if (r > 0) {
            nt->so.inCb(nt->so.inCbData, POPKCEL_OK);
        }
    }
}

int popkcel_initSocket(struct Popkcel_Socket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd)
{
    popkcel_initHandle((struct Popkcel_Handle*)sock, loop);
    if (socketType & POPKCEL_SOCKETTYPE_EXIST) {
        sock->fd = fd;
    }
    else {
        sock->fd = socket((socketType & POPKCEL_SOCKETTYPE_IPV6) ? AF_INET6 : AF_INET,
            (socketType & POPKCEL_SOCKETTYPE_TCP) ? SOCK_STREAM : SOCK_DGRAM, 0);
    }
    int f = fcntl(sock->fd, F_GETFL);
    if (f == -1)
        return POPKCEL_ERROR;
    f = fcntl(sock->fd, F_SETFL, f | O_NONBLOCK);
    if (f == -1)
        return POPKCEL_ERROR;
    popkcel_addHandle(loop, (struct Popkcel_Handle*)sock, 0);
    return POPKCEL_OK;
}

void popkcel_destroySocket(struct Popkcel_Socket* sock)
{
    close(sock->fd);
}

static void connectOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    int r;
    if (ev & POPKCEL_EVENT_ERROR)
        r = POPKCEL_ERROR;
    else
        r = POPKCEL_OK;
    sock->so.outRedo = NULL;
    sock->so.outCb(sock->so.outCbData, r);
}

int popkcel_tryConnect(struct Popkcel_Socket* sock, struct sockaddr* addr, socklen_t len, Popkcel_FuncCallback cb, void* data)
{
    int r = connect(sock->fd, addr, len);
    if (!r)
        return POPKCEL_OK;
    if (errno == EINPROGRESS) {
        if (!cb)
            return POPKCEL_WOULDBLOCK;
        if (sock->so.outRedo)
            return POPKCEL_PENDING;
        sock->so.outCb = cb;
        sock->so.outCbData = data;
        sock->so.outRedo = &connectOutRedo;
        sock->so.outRedoData = sock;
        return POPKCEL_WOULDBLOCK;
    }
    else
        return POPKCEL_ERROR;
}

static void writeOutRedoEnd(struct Popkcel_Socket* sock, int srv)
{
    sock->so.outRedo = 0;
    if (sock->buf)
        free(sock->buf);
    sock->so.outCb(sock->so.outCbData, srv);
}

static void writeOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    if (ev & POPKCEL_EVENT_ERROR) {
        writeOutRedoEnd(sock, POPKCEL_ERROR);
    }
    else {
        ssize_t r = write(sock->fd, sock->pos, sock->len);
        if ((size_t)r >= sock->len || r < 0) {
            writeOutRedoEnd(sock, r < 0 ? POPKCEL_ERROR : sock->len);
        }
        else {
            sock->pos += r;
            sock->len -= r;
        }
    }
}

ssize_t popkcel_tryWrite(struct Popkcel_Socket* sock, const char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    ssize_t r = write(sock->fd, buf, len);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            r = 0;
        else
            return POPKCEL_ERROR;
    }

    if ((size_t)r >= len)
        return len;

    if (!cb)
        return POPKCEL_WOULDBLOCK;
    if (sock->so.outRedo)
        return POPKCEL_PENDING;
    buf += r;
    len -= r;
    char* nbuf;
    const char* pos;
    volatile char spos;
#ifndef STACKGROWTHUP
    if (buf > &spos && buf < sock->loop->stackPos) {
#else
    if (buf < &spos && buf > sock->loop->stackPos) {
#endif
        nbuf = malloc(len);
        memcpy(nbuf, buf, len);
        pos = nbuf;
    }
    else {
        pos = buf;
        nbuf = NULL;
    }

    sock->so.outCb = cb;
    sock->so.outCbData = data;
    sock->so.outRedo = &writeOutRedo;
    sock->so.outRedoData = sock;
    sock->buf = nbuf;
    sock->pos = (char*)pos;
    sock->len = len;
    return POPKCEL_WOULDBLOCK;
}

static void sendToOutRedoEnd(struct Popkcel_Socket* sock, int srv)
{
    if (sock->bufCreated)
        free(sock->buf);
    if (sock->addrCreated)
        free(sock->addr);
    sock->so.outRedo = NULL;
    sock->so.outCb(sock->so.outCbData, srv);
}

static void sendToOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    if (ev & POPKCEL_EVENT_ERROR) {
        sendToOutRedoEnd(sock, POPKCEL_ERROR);
    }
    else {
        ssize_t r = sendto(sock->fd, sock->buf, sock->len, 0, sock->addr, sock->addrLen);
        sendToOutRedoEnd(sock, r);
    }
}

ssize_t popkcel_trySendto(struct Popkcel_Socket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data)
{
    ssize_t r = sendto(sock->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return POPKCEL_WOULDBLOCK;
        if (sock->so.outRedo)
            return POPKCEL_PENDING;

        char* nbuf;
        volatile char spos;
#ifndef STACKGROWTHUP
        if (buf > &spos && buf < sock->loop->stackPos) {
#else
        if (buf < &spos && buf > sock->loop->stackPos) {
#endif
            nbuf = malloc(len);
            memcpy(nbuf, buf, len);
            sock->bufCreated = 1;
        }
        else {
            nbuf = (char*)buf;
            sock->bufCreated = 0;
        }

#ifndef STACKGROWTHUP
        if ((char*)addr > &spos && (char*)addr < sock->loop->stackPos) {
#else
        if ((char*)addr < &spos && (char*)addr > sock->loop->stackPos) {
#endif
            sock->addr = malloc(addrLen);
            memcpy(sock->addr, addr, addrLen);
            sock->addrCreated = 1;
        }
        else {
            sock->addr = addr;
            sock->addrCreated = 0;
        }

        sock->addrLen = addrLen;
        sock->so.outCb = cb;
        sock->so.outCbData = data;
        sock->so.outRedo = &sendToOutRedo;
        sock->so.outRedoData = sock;
        sock->buf = nbuf;
        sock->len = len;
        return POPKCEL_WOULDBLOCK;
    }
    else
        return r;
}

static void readInRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    if (ev & POPKCEL_EVENT_IN) {
        ssize_t r = read(sock->fd, sock->rbuf, sock->rlen);
        sock->so.inRedo = NULL;
        if (sock->so.inCb)
            sock->so.inCb(sock->so.inCbData, r);
    }

    if (ev & POPKCEL_EVENT_ERROR) {
        sock->so.inRedo = NULL;
        if (sock->so.inCb)
            sock->so.inCb(sock->so.inCbData, POPKCEL_ERROR);
    }
}

ssize_t popkcel_tryRead(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    ssize_t r = read(sock->fd, buf, len);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return POPKCEL_WOULDBLOCK;
        if (sock->so.inRedo)
            return POPKCEL_PENDING;

        sock->so.inCb = cb;
        sock->so.inCbData = data;
        sock->so.inRedo = &readInRedo;
        sock->so.inRedoData = sock;
        sock->rbuf = buf;
        sock->rlen = len;
        return POPKCEL_WOULDBLOCK;
    }
    else
        return r;
}

static void recvFromInRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    if (ev & POPKCEL_EVENT_IN) {
        ssize_t r = recvfrom(sock->fd, sock->rbuf, sock->rlen, 0, sock->raddr, sock->raddrLen);
        sock->so.inRedo = 0;
        if (sock->so.inCb)
            sock->so.inCb(sock->so.inCbData, r);
    }

    if (ev & POPKCEL_EVENT_ERROR) {
        sock->so.inRedo = 0;
        if (sock->so.inCb)
            sock->so.inCb(sock->so.inCbData, POPKCEL_ERROR);
    }
}

ssize_t popkcel_tryRecvfrom(struct Popkcel_Socket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    ELCHECKIFONSTACK2(sock->loop, addr, "Do not allocate addr on stack!");
    ELCHECKIFONSTACK2(sock->loop, addrLen, "Do not allocate addrLen on stack!");
    ssize_t r = recvfrom(sock->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return POPKCEL_WOULDBLOCK;
        if (sock->so.inRedo)
            return POPKCEL_PENDING;

        sock->so.inCb = cb;
        sock->so.inCbData = data;
        sock->so.inRedo = &recvFromInRedo;
        sock->so.inRedoData = sock;
        sock->rbuf = buf;
        sock->rlen = len;
        sock->raddr = addr;
        sock->raddrLen = addrLen;
        return POPKCEL_WOULDBLOCK;
    }
    else
        return r;
}

/*
static void* runLoopCaller(void* data)
{
    popkcel_runLoop(data);
    return NULL;
}
*/
void popkcel_initListener(struct Popkcel_Listener* listener, struct Popkcel_Loop* loop, char ipv6, Popkcel_HandleType fd)
{
    popkcel_initHandle((struct Popkcel_Handle*)listener, loop);
    if (fd)
        listener->fd = fd;
    else
        listener->fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    listener->ipv6 = ipv6;
    int f = fcntl(listener->fd, F_GETFL);
    fcntl(listener->fd, F_SETFL, f | O_NONBLOCK);
}

void popkcel_destroyListener(struct Popkcel_Listener* listener)
{
    if (listener->fd)
        close(listener->fd);
}

static void listenerCb(void* data, intptr_t ev)
{
    if (ev & POPKCEL_EVENT_ERROR)
        return;
    struct Popkcel_Listener* listener = data;
    if (listener->funcAccept) {
        struct sockaddr_in6 addr;
        socklen_t len;
        if (listener->ipv6)
            len = sizeof(struct sockaddr_in6);
        else
            len = sizeof(struct sockaddr_in);

        for (;;) {
            int r = accept(listener->fd, (struct sockaddr*)&addr, &len);
            if (r >= 0)
                listener->funcAccept(listener->funcAcceptData, r, (struct sockaddr*)&addr, len);
            else {
                break;
            }
        }
    }
}

int popkcel_listen(struct Popkcel_Listener* listener, uint16_t port, int backlog)
{
    struct sockaddr_in6 addr;
    socklen_t addrLen;
    if (!listener->ipv6) {
        *(struct sockaddr_in*)&addr = popkcel_addressI(0, port);
        addrLen = sizeof(struct sockaddr_in);
    }
    else {
        memset(&addr, 0, sizeof(struct sockaddr_in6));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addrLen = sizeof(struct sockaddr_in6);
    }

    if (listener->ipv6 == 1) {
        int on = 1;
        setsockopt(listener->fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    }
    if (bind(listener->fd, (struct sockaddr*)&addr, addrLen))
        return POPKCEL_ERROR;
    if (listen(listener->fd, backlog))
        return POPKCEL_ERROR;
    else {
        int r = popkcel_addHandle(listener->loop, (struct Popkcel_Handle*)listener, POPKCEL_EVENT_IN);
        if (r != POPKCEL_OK)
            return POPKCEL_ERROR;
        listener->so.inRedo = &listenerCb;
        listener->so.inRedoData = listener;
        return POPKCEL_OK;
    }
}

void popkcel_initHandle(struct Popkcel_Handle* handle, struct Popkcel_Loop* loop)
{
    handle->loop = loop;
    memset(&handle->so, 0, sizeof(void*) * 4);
}

void popkcel_destroyLoop(struct Popkcel_Loop* loop)
{
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(loop->timers);
    while (it) {
        struct Popkcel_Rbtnode* it2 = it;
        it = popkcel_rbtNext(it);
        free(it2);
    }
    popkcel_destroySysTimer(&loop->sysTimer);
    popkcel__clearSo(loop);
    free(loop->events);
    close(loop->loopFd);
}

void popkcel__invokeLoop(void* data, intptr_t rv)
{
    (void)data;
    (void)rv;
}
