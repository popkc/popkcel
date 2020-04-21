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
#include <atomic>
#include <locale>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <iostream>

#ifdef NDEBUG
#    define CHECKMULTIOPERATION
#else
#    define CHECKMULTIOPERATION \
        assert(loop->running);  \
        assert(!mo->mapRv.count(this) && "A socket can have only one operation in a MultiOperation!");
#endif

#ifdef _MSC_VER
#    define FORCENOTINLINE __declspec(noinline)
#else
#    define FORCENOTINLINE __attribute__((noinline))
#endif

namespace popkcel {

thread_local Loop* threadLoop;

sockaddr_in address(uint32_t ip, uint16_t port)
{
    sockaddr_in addr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    return addr;
}

sockaddr_in address(const char* ip, uint16_t port)
{
    sockaddr_in addr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

PSSocket::PSSocket(Loop* loop, SocketType type)
    : Socket(loop, type)
{
#ifndef NDEBUG
    if (loop->running) {
        volatile char sp;
        CHECKIFONSTACK(this, "Do not allocate PSSocket on stack!");
    }
#endif
}

PSSocket::PSSocket(Loop* loop, HandleType fd)
    : Socket(loop, fd)
{
#ifndef NDEBUG
    if (loop->running) {
        volatile char sp;
        CHECKIFONSTACK(this, "Do not allocate PSSocket on stack!");
    }
#endif
}

PSSocket::~PSSocket()
{
    for (auto ri : readInfos) {
        delete ri;
    }
}

Context::~Context()
{
    if (savedStack)
        delete[] savedStack;
}

Handle::Handle(Loop* loop)
{
    this->loop = loop;
}

template <typename F, typename... Args>
static inline int multiCall(F f, PSSocket* so, int timeout, Args... args)
{
    std::unique_ptr<MultiOperation> mo(new MultiOperation(so->loop));
    (so->*f)(args..., mo.get());
    mo->wait(timeout);
    return mo->mapRv[so];
}

int PSSocket::connect(sockaddr* addr, int len, int timeout)
{
    return multiCall(&PSSocket::multiConnect, this, timeout, addr, len);
}

ssize_t PSSocket::write(const char* buf, size_t len, int timeout)
{
    return multiCall(&PSSocket::multiWrite, this, timeout, buf, len);
}

ssize_t PSSocket::sendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, int timeout)
{
    return multiCall(&PSSocket::multiSendto, this, timeout, buf, len, addr, addrLen);
}

ssize_t PSSocket::read(char* buf, size_t len, int timeout)
{
    return multiCall(&PSSocket::multiRead, this, timeout, buf, len);
}

ssize_t PSSocket::readFor(char* buf, size_t len, int timeout)
{
    return multiCall(&PSSocket::multiReadFor, this, timeout, buf, len);
}

ssize_t PSSocket::recvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, int timeout)
{
    return multiCall(&PSSocket::multiRecvfrom, this, timeout, buf, len, addr, addrLen);
}

void PSSocket::multiConnect(sockaddr* addr, int len, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    int r = tryConnect(addr, len, std::bind(&PSSocket::generalCb, this, std::placeholders::_1));
    mo->mapRv[this] = r;
    if (r == SOCKRV_WOULDBLOCK) {
        mo->count++;
        this->mo = mo;
    }
}

void PSSocket::multiWrite(const char* buf, size_t len, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    ssize_t r = tryWrite(buf, len, std::bind(&PSSocket::generalCb, this, std::placeholders::_1));
    mo->mapRv[this] = (int)r;
    if (r == SOCKRV_WOULDBLOCK)
        r = 0;
    if (r < 0 || (size_t)r >= len)
        return;

    mo->count++;
    this->mo = mo;
}

void PSSocket::multiSendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    ssize_t r = trySendto(buf, len, addr, addrLen, std::bind(&PSSocket::generalCb, this, std::placeholders::_1));
    mo->mapRv[this] = (int)r;
    if (r == SOCKRV_WOULDBLOCK) {
        mo->count++;
        this->mo = mo;
    }
}

void PSSocket::multiRead(char* buf, size_t len, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    ssize_t r = tryRead(buf, len, std::bind(&PSSocket::generalCb, this, std::placeholders::_1));
    mo->mapRv[this] = (int)r;
    if (r == SOCKRV_WOULDBLOCK) {
        mo->count++;
        this->mo = mo;
    }
}

void PSSocket::multiReadFor(char* buf, size_t len, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    ReadInfo* ri = new ReadInfo;
    ssize_t r = tryRead(buf, len, std::bind(&PSSocket::readForCb, this, ri, std::placeholders::_1));
    mo->mapRv[this] = (int)r;
    if (r == SOCKRV_WOULDBLOCK)
        r = 0;
    if (r < 0 || (size_t)r >= len) {
        delete ri;
        return;
    }

    mo->count++;
    this->mo = mo;
    ri->buf = buf + r;
    ri->len = len - r;
    this->readInfos.push_back(ri);
}

void PSSocket::multiRecvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, MultiOperation* mo)
{
    CHECKMULTIOPERATION
    ssize_t r = tryRecvfrom(buf, len, addr, addrLen, std::bind(&PSSocket::generalCb, this, std::placeholders::_1));
    mo->mapRv[this] = (int)r;
    if (r == SOCKRV_WOULDBLOCK) {
        mo->count++;
        this->mo = mo;
    }
}

void PSSocket::generalCb(intptr_t rv)
{
    if (!mo)
        return;
#ifdef _WIN32
    if (rv == SOCKRV_OK)
        this->mo->mapRv[this] = loop->numOfBytes;
    else
        this->mo->mapRv[this] = SOCKRV_ERROR;
#else
    this->mo->mapRv[this] = rv;
#endif
    this->mo->count--;
    this->mo->curSocket = this;
    this->mo->checkCount();
}

void PSSocket::readForCb(ReadInfo* ri, intptr_t rv)
{
    if (rv < 0 || (size_t)rv >= ri->len) {
    ok:;
        auto it = std::find(readInfos.begin(), readInfos.end(), ri);
        if (it != readInfos.end())
            readInfos.erase(it);
        delete ri;
        generalCb(rv);
    }
    else {
        ri->buf += rv;
        ri->len -= rv;
        ssize_t r = tryRead(ri->buf, ri->len, std::bind(&PSSocket::readForCb, this, ri, std::placeholders::_1));
        if (r == SOCKRV_WOULDBLOCK)
            r = 0;
        if (r < 0 || (size_t)r >= ri->len)
            goto ok;
        ri->buf += r;
        ri->len -= r;
    }
}

/*linux下amd64架构，直接用局部变量的地址当stack pointer会出错，似乎保存的数据少了。别的平台不清楚，不管那么多，
我只要用下级函数获取stack pointer，就能把整个函数的stack都包括进去，代价是会多复制一些没用的数据，但在不用汇编的情况下这个恐怕是无法避免的。
我试过用alloca，复制的量比这个还多。*/
FORCENOTINLINE static void getStackPos(Context* context)
{
    volatile char a;
    context->stackPos = (char*)&a;
}

//因为获得stack pointer是在下级的函数getStackPos里，所以恢复时也得调用下级函数，再调用memcpy。
//直接调用memcpy的话memcpy本身的stack会被覆盖，导致崩溃。
FORCENOTINLINE static void doMemcpy(char* start, char* end) noexcept
{
    memcpy(start, threadLoop->curContext->savedStack, end - start);
    //stack恢复后，返回地址已不可信，所以直接用longjmp回到上级函数中。
    POPKCLONGJMP(threadLoop->curContext->jmpBuf, 2);
}

void suspend(Context* context) noexcept
{
    assert(threadLoop->running && "loop must be running!");
    switch (POPKCSETJMP(context->jmpBuf)) {
    case 1: {
        //resume后，会执行这里，此时context的值已不可信，应该用threadLoop->curContext。通常stack是向下扩展的，但这里也支持向上扩展的架构。
#ifndef STACKGROWTHUP
        //memcpy(threadLoop->curContext->stackPos, threadLoop->curContext->savedStack, threadLoop->stackPos - threadLoop->curContext->stackPos);
        doMemcpy(threadLoop->curContext->stackPos, threadLoop->stackPos);
#else
        //memcpy(threadLoop->stackPos, threadLoop->curContext->savedStack, threadLoop->curContext->stackPos - threadLoop->stackPos);
        doMemcpy(threadLoop->stackPos, threadLoop->curContext->stackPos);
#endif
    } break;
    case 0: {
        //void* sp = alloca(1);
        size_t stackSize;
        getStackPos(context);
#ifndef STACKGROWTHUP
        //context->stackPos = (char*)sp + 1;
        stackSize = threadLoop->stackPos - context->stackPos;
        //printf("%d\n", stackSize);
        context->savedStack = new char[stackSize];
        memcpy(context->savedStack, context->stackPos, stackSize);
#else
        //context->stackPos = (char*)sp;
        stackSize = context->stackPos - threadLoop->stackPos;
        context->savedStack = new char[stackSize];
        memcpy(context->savedStack, threadLoop->stackPos, stackSize);
#endif
        POPKCLONGJMP(threadLoop->jmpBuf, 1);
    } break;
    default:
        break;
    }
}

void resume(Context* context) noexcept
{
    threadLoop->curContext = context;
    POPKCLONGJMP(context->jmpBuf, 1);
}

void Loop::stopTimer()
{
    sysTimer->stopTimer();
}

void Loop::setupTimer()
{
    sysTimer = new SysTimer(this);
}

int Loop::checkTimers()
{
    int rv = -1;
    auto ctp = std::chrono::steady_clock::now();
    for (auto it = timers.begin(); it != timers.end();) {
        if (it->first <= ctp) {
            Timer* tm = it->second;
            it = timers.erase(it);
            tm->iter = timers.end();
            if (tm->funcCb) {
                tm->funcCb((intptr_t)&tm);
            }

            if (tm) {
                if (tm->interval > 0) {
                    auto nctp = ctp + std::chrono::milliseconds(tm->interval);
                    tm->iter = timers.emplace(nctp, tm);
                }
            }
        }
        else {
            rv = (int)std::chrono::duration_cast<std::chrono::milliseconds>(it->first - ctp).count();
            break;
        }
    }
    return rv;
}

void Loop::setupFirstTimeCb(const std::function<void()>& fft)
{
    Notifier* nt = new Notifier(this);
    nt->setCb([nt, fft](intptr_t) {
        fft();
        delete nt;
    });
    nt->notify();
}

void Loop::removeMo(MultiOperation* mo)
{
    if (!running)
        return;
    auto it = moSet.find(mo);
    if (it != moSet.end()) {
        moSet.erase(it);
    }
}

MultiOperation::MultiOperation(Loop* loop)
{
#ifndef NDEBUG
    if (loop->running) {
        ELCHECKIFONSTACK(this, "Do not allocate MultiOperation on stack!");
    }
#endif
    this->loop = loop;
    loop->moSet.insert(this);
}

MultiOperation::~MultiOperation()
{
    if (timer) {
        delete timer;
    }
    for (auto& p : mapRv) {
        if (p.first) {
#ifdef _WIN32
            if (p.first->ic) {
                p.first->ic->funcCb = 0;
                p.first->ic = nullptr;
            }
#endif
            p.first->mo = nullptr;
        }
    }
    loop->removeMo(this);
}

void MultiOperation::checkCount()
{
    if (multiCallback || count <= 0) {
        resume(&context);
    }
}

void MultiOperation::wait(int timeout, int multiCallback)
{
    timeOuted = false;
    this->multiCallback = multiCallback;
    if (!count)
        return;
    if (timeout > 0) {
        assert(!timer && "You can only wait once! Please delete and recreate a MultiOperation object.");
        timer = new Timer(loop);
        timer->setTimer(timeout);
        timer->funcCb = [this](intptr_t rv) {
            if (this->count > 0) {
                this->timeOuted = true;
                this->curSocket = nullptr;
                resume(&this->context);
            }
        };
    }
    suspend(&context);
}

void MultiOperation::reBlock() noexcept
{
    if (count <= 0 || timeOuted)
        return;
    POPKCLONGJMP(loop->jmpBuf, 1);
}

void LoopPool::detach()
{
    threads.resize(loops.size());
    for (size_t i = 0; i < loops.size(); i++) {
        threads[i] = std::thread(runLoop, &loops[i]);
        threads[i].detach();
    }
}

int LoopPool::run()
{
    threads.resize(loops.size() - 1);
    for (size_t i = 1; i < loops.size(); i++) {
        threads[i - 1] = std::thread(runLoop, &loops[i]);
        threads[i - 1].detach();
    }
    return runLoop(&loops[0]);
}

void LoopPool::closeAll()
{
    for (auto& l : loops) {
        l.running = false;
    }
}

Timer::Timer(Loop* loop)
{
    this->loop = loop;
    iter = loop->timers.end();
}

Timer::~Timer()
{
    if (iter != loop->timers.end())
        loop->timers.erase(iter);
}

void Timer::setTimer(uint32_t timeout, uint32_t interval)
{
    auto ctp = std::chrono::steady_clock::now();
    ctp += std::chrono::milliseconds(timeout);
    this->interval = interval;
    auto it = loop->timers.begin();
    if (it == loop->timers.end() || it->first > ctp) {
        loop->setTimeout(timeout);
    }

    if (iter != loop->timers.end())
        loop->timers.erase(iter);
    iter = loop->timers.emplace(ctp, this);
}

void Timer::stopTimer()
{
    if (iter != loop->timers.end()) {
        loop->timers.erase(iter);
        iter = loop->timers.end();
    }
}

}
