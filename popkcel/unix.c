/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
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

#define WRITEBUFFERCOMMONFIELD        \
    struct Popkcel_WriteBuffer* next; \
    Popkcel_FuncCallback outCb;       \
    void* cbData;

struct Popkcel_SendToBuffer
{
    WRITEBUFFERCOMMONFIELD
    struct sockaddr_in6 addr;
    socklen_t addrLen;
    size_t bufLen;
    char buffer[];
};

struct Popkcel_WriteBuffer
{
    WRITEBUFFERCOMMONFIELD
    size_t bufLen;
    size_t bytesWritten;
    char buffer[];
};

int popkcel_init()
{
    signal(SIGPIPE, SIG_IGN);
    popkcel__globalInit();
    return 0;
}

void popkcel__eventCall(struct Popkcel_SingleOperation* so, int event)
{
    if (event & POPKCEL_EVENT_ERROR) {
        if (so->inRedo) {
            if (so->inRedo(so->inRedoData, event))
                return;
        }
        if (so->outRedo)
            so->outRedo(so->outRedoData, event);
    }
    else {
        if ((event & POPKCEL_EVENT_IN) && so->inRedo) {
            if (so->inRedo(so->inRedoData, event))
                return;
        }
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

int popkcel_closeSocket(Popkcel_HandleType fd)
{
    return close(fd);
}

int popkcel_initSocket(struct Popkcel_Socket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd)
{
    popkcel_initHandle((struct Popkcel_Handle*)sock, loop);
    sock->writeBuffer = NULL;
    sock->ipv6 = (socketType & POPKCEL_SOCKETTYPE_IPV6) ? 1 : 0;
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

static void clearWriteBuffer(struct Popkcel_Socket* sock)
{
    struct Popkcel_WriteBuffer *buf, *old;
    buf = sock->writeBuffer;
    while (buf) {
        old = buf;
        buf = buf->next;
        free(old);
    }
}

void popkcel_destroySocket(struct Popkcel_Socket* sock)
{
    close(sock->fd);
    clearWriteBuffer(sock);
}

static int connectOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    int r;
    if (ev & POPKCEL_EVENT_ERROR)
        r = POPKCEL_ERROR;
    else
        r = POPKCEL_OK;
    sock->so.outRedo = NULL;
    sock->so.outCb(sock->so.outCbData, r);
    return 0;
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

static int writeOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    sock->so.outRedo = NULL;
    if (ev & POPKCEL_EVENT_ERROR) {
        while (sock->writeBuffer) {
            struct Popkcel_WriteBuffer* wb = sock->writeBuffer;
            sock->writeBuffer = wb->next;
            if (wb->outCb) {
                wb->outCb(wb->cbData, POPKCEL_ERROR);
                free(wb);
                return 0; //error只通知一次，收到通知后用户应自行destroy socket
            }
            else
                free(wb);
        }
    }
    else {
        while (sock->writeBuffer) {
            struct Popkcel_WriteBuffer* wb = sock->writeBuffer;
            sock->writeBuffer = wb->next;
            ssize_t r = write(sock->fd, wb->buffer + wb->bytesWritten, wb->bufLen - wb->bytesWritten);
            if (r >= wb->bufLen - wb->bytesWritten) {
                if (wb->outCb)
                    wb->outCb(wb->cbData, wb->bufLen);
                free(wb);
            }
            else {
                if (r == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        r = 0;
                    else {
                        if (wb->outCb)
                            wb->outCb(wb->cbData, POPKCEL_ERROR);
                        free(wb);
                        return 0;
                    }
                }

                wb->bytesWritten += r;
                sock->writeBuffer = wb;
                sock->so.outRedo = &writeOutRedo;
                return 0;
            }
        }
    }
    return 0;
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

    if (sock->so.outRedo) {
        if (sock->so.outRedo != &writeOutRedo)
            return POPKCEL_PENDING;
    }
    else {
        sock->so.outRedo = &writeOutRedo;
        sock->so.outRedoData = sock;
    }

    buf += r;
    len -= r;
    struct Popkcel_WriteBuffer* wb = malloc(sizeof(struct Popkcel_WriteBuffer) + len);
    wb->bufLen = len;
    wb->bytesWritten = 0;
    wb->outCb = cb;
    wb->cbData = data;
    wb->next = NULL;
    memcpy(wb->buffer, buf, len);
    struct Popkcel_WriteBuffer** pwb = (struct Popkcel_WriteBuffer**)&sock->writeBuffer;
    while (*pwb) {
        pwb = &(*pwb)->next;
    }
    *pwb = wb;
    return POPKCEL_WOULDBLOCK;
}

static int sendToOutRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    sock->so.outRedo = NULL;
    if (ev & POPKCEL_EVENT_ERROR) {
        while (sock->writeBuffer) {
            struct Popkcel_SendToBuffer* sb = sock->writeBuffer;
            sock->writeBuffer = sb->next;
            if (sb->outCb) {
                sb->outCb(sb->cbData, POPKCEL_ERROR);
                free(sb);
                return 0; //error只通知一次，收到通知后用户应自行destroy socket
            }
            else
                free(sb);
        }
    }
    else {
        while (sock->writeBuffer) {
            struct Popkcel_SendToBuffer* sb = sock->writeBuffer;
            sock->writeBuffer = sb->next;
            ssize_t r = sendto(sock->fd, sb->buffer, sb->bufLen, 0, (struct sockaddr*)&sb->addr, sb->addrLen);
            if (r == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    sock->writeBuffer = sb;
                    sock->so.outRedo = &sendToOutRedo;
                }
                else {
                    if (sb->outCb)
                        sb->outCb(sb->cbData, POPKCEL_ERROR);
                    free(sb);
                }
                return 0;
            }
            else {
                if (sb->outCb)
                    sb->outCb(sb->cbData, r);
                free(sb);
            }
        }
    }
    return 0;
}

ssize_t popkcel_trySendto(struct Popkcel_Socket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data)
{
    ssize_t r = sendto(sock->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (sock->so.outRedo) {
            if (sock->so.outRedo != &sendToOutRedo)
                return POPKCEL_PENDING;
        }
        else {
            sock->so.outRedo = &sendToOutRedo;
            sock->so.outRedoData = sock;
        }

        struct Popkcel_SendToBuffer* sb = malloc(sizeof(struct Popkcel_SendToBuffer) + len);
        memcpy(&sb->addr, addr, addrLen);
        sb->addrLen = addrLen;
        sb->bufLen = len;
        sb->next = NULL;
        sb->outCb = cb;
        sb->cbData = data;
        memcpy(sb->buffer, buf, len);
        struct Popkcel_SendToBuffer** psb = (struct Popkcel_SendToBuffer**)&sock->writeBuffer;
        while (*psb) {
            psb = (struct Popkcel_SendToBuffer**)&(*psb)->next;
        }
        *psb = sb;
        return POPKCEL_WOULDBLOCK;
    }
    else
        return r;
}

static int readInRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    int retv;
    if (ev & POPKCEL_EVENT_IN) {
        ssize_t r = read(sock->fd, sock->rbuf, sock->rlen);
        sock->so.inRedo = NULL;
        if (sock->so.inCb) {
            retv = sock->so.inCb(sock->so.inCbData, r);
            if (retv)
                return retv;
        }
    }

    if (ev & POPKCEL_EVENT_ERROR) {
        sock->so.inRedo = NULL;
        if (sock->so.inCb) {
            retv = sock->so.inCb(sock->so.inCbData, POPKCEL_ERROR);
            if (retv)
                return retv;
        }
    }
    return 0;
}

ssize_t popkcel_tryRead(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    ssize_t r = read(sock->fd, buf, len);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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

static int recvFromInRedo(void* data, intptr_t ev)
{
    struct Popkcel_Socket* sock = data;
    int retv;
    if (ev & POPKCEL_EVENT_IN) {
        ssize_t r = recvfrom(sock->fd, sock->rbuf, sock->rlen, 0, sock->raddr, sock->raddrLen);
        sock->so.inRedo = 0;
        if (sock->so.inCb) {
            retv = sock->so.inCb(sock->so.inCbData, r);
            if (retv)
                return retv;
        }
    }

    if (ev & POPKCEL_EVENT_ERROR) {
        sock->so.inRedo = 0;
        if (sock->so.inCb) {
            retv = sock->so.inCb(sock->so.inCbData, POPKCEL_ERROR);
            if (retv)
                return retv;
        }
    }
    return 0;
}

ssize_t popkcel_tryRecvfrom(struct Popkcel_Socket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb, void* data)
{
    ELCHECKIFONSTACK(sock->loop, buf, "Do not allocate buffer on stack!");
    ELCHECKIFONSTACK2(sock->loop, addr, "Do not allocate addr on stack!");
    ELCHECKIFONSTACK2(sock->loop, addrLen, "Do not allocate addrLen on stack!");
    ssize_t r = recvfrom(sock->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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

static int listenerCb(void* data, intptr_t ev)
{
    if (ev & POPKCEL_EVENT_ERROR)
        return 0;
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
    return 0;
}

int popkcel_listen(struct Popkcel_Listener* listener, uint16_t port, int backlog)
{
    popkcel_bind((struct Popkcel_Socket*)listener, port);
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
    memset(&handle->so, 0, sizeof(struct Popkcel_SingleOperation));
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
    free(loop->events);
    close(loop->loopFd);
}

int popkcel__invokeLoop(void* data, intptr_t rv)
{
    (void)data;
    (void)rv;
    return 0;
}
