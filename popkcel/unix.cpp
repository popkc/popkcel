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

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

namespace popkcel {
int closeFd(HandleType fd)
{
    return close(fd);
}

void Loop::lock()
{
    bool f;
    for (;;) {
        f = false;
        if (spinLock.compare_exchange_weak(f, true))
            break;
    }
}

void Loop::unlock()
{
    spinLock.store(false);
}

Socket::Socket(Loop* loop, SocketType type)
    : Handle(loop)
{
#ifndef __linux__
    handleTypeEnum = HT_SOCKET;
#endif
    this->fd = socket(AF_INET, type == SOCKETTYPE_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    int f = fcntl(this->fd, F_GETFL);
    fcntl(this->fd, F_SETFL, f | O_NONBLOCK);
    addToLoop();
}

Socket::Socket(Loop* loop, HandleType fd)
    : Handle(loop)
{
#ifndef __linux__
    handleTypeEnum = HT_SOCKET;
#endif
    this->fd = fd;
    int f = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, f | O_NONBLOCK);
    addToLoop();
}

int Socket::tryConnect(sockaddr* addr, socklen_t len, const FuncCallback& cb)
{
    int r = ::connect(this->fd, addr, len);
    if (!r)
        return SOCKRV_OK;
    if (errno == EINPROGRESS) {
        if (!cb)
            return SOCKRV_WOULDBLOCK;
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so)
            return SOCKRV_ERROR;
        if (so->outRedo)
            return SOCKRV_PENDING;
        so->outCb = cb;
        so->outRedo = [this](int ev) {
            ELLOCKLOOP(this->loop);
            auto so = this->loop->operations[this->fd].get();
            ELUNLOCKLOOP(this->loop);
            if (!so)
                return;
            int r;
            if (ev & (EVENT_ERROR | EVENT_DISCONNECTED))
                r = SOCKRV_ERROR;
            else
                r = SOCKRV_OK;
            so->outRedo = 0;
            so->outCb(r);
        };
        return SOCKRV_WOULDBLOCK;
    }
    else
        return SOCKRV_ERROR;
}

ssize_t Socket::tryWrite(const char* buf, size_t len, const FuncCallback& cb)
{
    ssize_t r = ::write(this->fd, buf, len);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            r = 0;
        else
            return SOCKRV_ERROR;
    }

    if ((size_t)r >= len)
        return len;

    if (!cb)
        return SOCKRV_WOULDBLOCK;
    ELLOCKLOOP(this->loop);
    auto so = this->loop->operations[this->fd].get();
    ELUNLOCKLOOP(this->loop);
    if (!so)
        return SOCKRV_ERROR;
    if (so->outRedo)
        return SOCKRV_PENDING;
    buf += r;
    len -= r;
    char* nbuf;
    const char* pos;
    volatile char spos;
#ifndef STACKGROWTHUP
    if (buf > (void*)&spos && buf < (void*)loop->stackPos) {
#else
    if (buf < (void*)&spos && buf > (void*)loop->stackPos) {
#endif
        nbuf = new char[len];
        memcpy(nbuf, buf, len);
        pos = nbuf;
    }
    else {
        pos = buf;
        nbuf = nullptr;
    }

    so->outCb = cb;
    so->outRedo = [this, nbuf, pos, len](int ev) mutable {
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so)
            return;
        if (ev & (EVENT_DISCONNECTED | EVENT_ERROR)) {
            so->outRedo = 0;
            so->outCb(SOCKRV_ERROR);
            return;
        }
        ssize_t r = ::write(this->fd, pos, len);
        if ((size_t)r >= len || r < 0) {
            if (nbuf)
                delete[] nbuf;
            so->outRedo = 0;
            so->outCb(r < 0 ? SOCKRV_ERROR : len);
        }
        else {
            pos += r;
            len -= r;
        }
    };
    return SOCKRV_WOULDBLOCK;
}

ssize_t Socket::trySendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, const FuncCallback& cb)
{
    ssize_t r = ::sendto(this->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return SOCKRV_WOULDBLOCK;
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so)
            return SOCKRV_ERROR;
        if (so->outRedo || !cb)
            return SOCKRV_PENDING;

        char* nbuf;
        sockaddr* naddr;
        bool cnb, cna;
        volatile char spos;
#ifndef STACKGROWTHUP
        if (buf > (void*)&spos && buf < (void*)loop->stackPos) {
#else
        if (buf < (void*)&spos && buf > (void*)loop->stackPos) {
#endif
            nbuf = new char[len];
            memcpy(nbuf, buf, len);
            cnb = true;
        }
        else {
            nbuf = (char*)buf;
            cnb = false;
        }

#ifndef STACKGROWTHUP
        if (addr > (void*)&spos && addr < (void*)loop->stackPos) {
#else
        if (addr < (void*)&spos && addr > (void*)loop->stackPos) {
#endif
            naddr = new sockaddr;
            memcpy(naddr, addr, addrLen);
            cna = true;
        }
        else {
            naddr = addr;
            cna = false;
        }

        so->outCb = cb;
        so->outRedo = [this, nbuf, len, naddr, addrLen, cnb, cna](int ev) {
            ELLOCKLOOP(this->loop);
            auto so = this->loop->operations[this->fd].get();
            ELUNLOCKLOOP(this->loop);
            if (ev & (EVENT_DISCONNECTED | EVENT_ERROR)) {
                so->outRedo = 0;
                so->outCb(SOCKRV_ERROR);
            }
            else {
                ssize_t r = ::sendto(this->fd, nbuf, len, 0, naddr, addrLen);
                if (cnb)
                    delete[] nbuf;
                if (cna)
                    delete naddr;
                so->outRedo = 0;
                so->outCb(r);
            }
        };
        return SOCKRV_WOULDBLOCK;
    }
    else
        return r;
}

ssize_t Socket::tryRead(char* buf, size_t len, const FuncCallback& cb)
{
    ELCHECKIFONSTACK(buf, "Do not allocate buffer on stack!");
    ssize_t r = ::read(this->fd, buf, len);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return SOCKRV_WOULDBLOCK;
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so)
            return SOCKRV_ERROR;
        if (so->inRedo)
            return SOCKRV_PENDING;
        so->inCb = cb;
        so->inRedo = [this, buf, len](int ev) {
            ELLOCKLOOP(this->loop);
            auto so = this->loop->operations[this->fd].get();
            ELUNLOCKLOOP(this->loop);
            if (!so)
                return;
            if (ev & EVENT_IN) {
                ssize_t r = ::read(this->fd, buf, len);
                so->inRedo = 0;
                so->inCb(r);
            }
            else {
                so->inRedo = 0;
                so->inCb(SOCKRV_ERROR);
            }
        };
        return SOCKRV_WOULDBLOCK;
    }
    else
        return r;
}

ssize_t Socket::tryRecvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, const FuncCallback& cb)
{
    ELCHECKIFONSTACK(buf, "Do not allocate buffer on stack!");
    CHECKIFONSTACK(addr, "Do not allocate addr on stack!");
    CHECKIFONSTACK(addrLen, "Do not allocate addrLen on stack!");
    ssize_t r = ::recvfrom(this->fd, buf, len, 0, addr, addrLen);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!cb)
            return SOCKRV_WOULDBLOCK;
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so)
            return SOCKRV_ERROR;
        if (so->inRedo)
            return SOCKRV_PENDING;
        so->inCb = cb;
        so->inRedo = [this, buf, len, addr, addrLen](int ev) {
            ELLOCKLOOP(this->loop);
            auto so = this->loop->operations[this->fd].get();
            ELUNLOCKLOOP(this->loop);
            if (!so)
                return;
            if (ev & EVENT_IN) {
                ssize_t r = ::recvfrom(this->fd, buf, len, 0, addr, addrLen);
                so->inRedo = 0;
                so->inCb(r);
            }
            else {
                so->inRedo = 0;
                so->inCb(SOCKRV_ERROR);
            }
        };
        return SOCKRV_WOULDBLOCK;
    }
    else
        return r;
}

void Notifier::inRedo(int ev)
{
    if (ev & EVENT_IN) {
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (!so || !so->inCb)
            return;
        char buf[8];
        int r = ::read(this->fd, buf, 8);
        if (r > 0) {
            so->inCb(SOCKRV_OK);
        }
    }
}

Handle::~Handle()
{
    if (fd) {
        close(fd);
        ELLOCKLOOP(this->loop);
        loop->operations.erase(fd);
        ELUNLOCKLOOP(this->loop);
    }
}

Listener ::Listener(Loop* loop, HandleType fd)
    : Handle(loop)
{
#ifndef __linux__
    handleTypeEnum = HT_LISTENSOCKET;
#endif
    if (fd)
        this->fd = fd;
    else
        this->fd = socket(AF_INET, SOCK_STREAM, 0);
    int f = fcntl(this->fd, F_GETFL);
    fcntl(this->fd, F_SETFL, f | O_NONBLOCK);
}

int Listener::listen(uint16_t port, int backlog)
{
    auto addr = address((uint32_t)0, port);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)))
        return SOCKRV_ERROR;
    if (::listen(fd, backlog))
        return SOCKRV_ERROR;
    else {
		auto so=addToLoop();
        if (!so)
            return SOCKRV_ERROR;
        so->inRedo = [this](int ev) {
            if (ev & (EVENT_DISCONNECTED | EVENT_ERROR))
                return;
            if (this->funcAccept) {
                sockaddr addr;
                for (;;) {
                    socklen_t len = sizeof(sockaddr);
                    int r = ::accept(this->fd, &addr, &len);
                    if (r >= 0)
                        this->funcAccept(r, &addr, len);
                    else {
                        break;
                    }
                }
            }
        };
        return SOCKRV_OK;
    }
}

void LoopPool::moveSocket(size_t threadNum, Socket* socket)
{
    if (threadNum >= loops.size())
        return;
    Loop* newl = &loops[threadNum];
    Loop* oldl = socket->loop;
    if (newl == oldl)
        return;
    std::shared_ptr<SingleOperation> so;
    oldl->removeHandle(socket);
    ELLOCKLOOP(oldl);
    auto it = oldl->operations.find(socket->fd);
    if (it != oldl->operations.end()) {
        so = it->second;
        oldl->operations.erase(it);
    }
    ELUNLOCKLOOP(oldl);
    socket->loop = newl;
    newl->addHandle(socket);
    ELLOCKLOOP(newl);
    newl->operations[socket->fd] = so;
    ELUNLOCKLOOP(newl);
}

Loop::~Loop()
{
    if (!loopFd)
        return;
    delete[] events;
    for (auto pmo : moSet) {
        delete pmo;
    }
    delete sysTimer;
    close(loopFd);
}

void Loop::eventCall(HandleType fd, int event)
{
    ELLOCKLOOP(this);
    auto it = this->operations.find(fd);
    if (it != this->operations.end() && it->second) {
        auto p = it->second;
        ELUNLOCKLOOP(this);
        if (event & (EVENT_ERROR | EVENT_DISCONNECTED)) {
            if (p->inRedo)
                p->inRedo(event);
            if (p->outRedo)
                p->outRedo(event);
        }
        else {
            if (event & EVENT_IN) {
                if (p->inRedo)
                    p->inRedo(event);
            }
            if (event & EVENT_OUT) {
                if (p->outRedo)
                    p->outRedo(event);
            }
        }
    }
    else {
        ELUNLOCKLOOP(this);
    }
}

void Loop::setTimeout(uint32_t timeout)
{
    sysTimer->setTimer(timeout, [this](intptr_t) {
        this->checkTimers();
    });
}

SingleOperation* Handle::addToLoop()
{
    int r = loop->addHandle(this);
    ELLOCKLOOP(loop);
    auto& so = loop->operations[fd];
    so.reset(new SingleOperation);
    auto p = so.get();
    ELUNLOCKLOOP(loop);
    return p;
}

LoopPool::LoopPool(size_t numOfLoops, size_t maxEvents)
    : loops(numOfLoops ? numOfLoops : std::thread::hardware_concurrency(), Loop(this, maxEvents))
{
}

int init()
{
    signal(SIGPIPE, SIG_IGN);
    return 0;
}
}
