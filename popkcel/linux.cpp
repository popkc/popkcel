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
#include <bits/types/struct_itimerspec.h>
#include <ctime>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
namespace popkcel {

namespace {
    int translateEvent(int event)
    {
        int tev = 0;
        if (event & EPOLLIN)
            tev |= EVENT_IN;
        if (event & EPOLLOUT)
            tev |= EVENT_OUT;
        if (event & EPOLLERR)
            tev |= EVENT_ERROR;
        if (event & EPOLLHUP)
            tev |= EVENT_DISCONNECTED;
        return tev;
    }
}

Loop::Loop(LoopPool* loopPool, size_t maxEvents)
    : spinLock(false)
{
    this->loopPool = loopPool;
    this->maxEvents = maxEvents;
    loopFd = epoll_create(maxEvents);
    events = new epoll_event[maxEvents];
    setupTimer();
}

Loop::Loop(const Loop& loop)
    : spinLock(false)
{
    this->loopPool = loop.loopPool;
    this->maxEvents = loop.maxEvents;
    loopFd = epoll_create(maxEvents);
    events = new epoll_event[maxEvents];
    setupTimer();
}

int Loop::addHandle(Handle* handle)
{
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = handle->fd;
    return epoll_ctl(loopFd, EPOLL_CTL_ADD, handle->fd, &ev);
}

int Loop::removeHandle(Handle* handle)
{
    return epoll_ctl(loopFd, EPOLL_CTL_DEL, handle->fd, NULL);
}

int runLoop(Loop* loop)
{
    volatile char stackPos;
    loop->stackPos = (char*)&stackPos;
    threadLoop = loop;
    loop->inited = false;
    loop->running = true;
    do {
        int r = loop->checkTimers();
        loop->numOfEvents = epoll_wait(loop->loopFd, loop->events, loop->maxEvents, r);
        loop->curIndex = 0;
        if (!loop->inited) {
            loop->inited = true;
            if (setjmp(loop->jmpBuf)) {
                loop = threadLoop;
                loop->curIndex++;
            }
        }

        while (loop->curIndex < loop->numOfEvents) {
            int fd = loop->events[loop->curIndex].data.fd;
            int event = translateEvent(loop->events[loop->curIndex].events);
            loop->eventCall(fd, event);
            loop->curIndex++;
        }
    } while (loop->running);
    return 0;
}

Notifier::Notifier(Loop* loop, HandleType fd)
    : Handle(loop)
{
    if (fd)
        this->fd = fd;
    else
        this->fd = eventfd(0, EFD_NONBLOCK);
    addToLoop();
}

int Notifier::notify()
{
    uint64_t i = 1;
    int r = write(this->fd, &i, 8);
    if (r >= 0)
        return r;
    if (errno == EWOULDBLOCK || errno == EAGAIN)
        return SOCKRV_WOULDBLOCK;
    else
        return SOCKRV_ERROR;
}

void Notifier::setCb(const FuncCallback& cb)
{
    ELLOCKLOOP(this->loop);
    auto so = this->loop->operations[this->fd].get();
    ELUNLOCKLOOP(this->loop);
    if (!so)
        return;
    so->inCb = cb;
    so->inRedo = std::bind(&Notifier::inRedo, this, std::placeholders::_1);
}

Notifier::~Notifier() {}

SysTimer::SysTimer(Loop* loop)
    : Handle(loop)
{
    this->fd = timerfd_create(CLOCK_MONOTONIC, 0);
    auto so = addToLoop();
    so->inRedo = [this](int ev) {
        ELLOCKLOOP(this->loop);
        auto so = this->loop->operations[this->fd].get();
        ELUNLOCKLOOP(this->loop);
        if (so && so->inCb)
            so->inCb((ev & (EVENT_DISCONNECTED | EVENT_ERROR)) ? SOCKRV_ERROR : SOCKRV_OK);
    };
}

void SysTimer::setTimer(int timeout, const FuncCallback& cb, bool periodic)
{
    div_t dt = div(timeout, 1000);
    itimerspec its;
    its.it_value.tv_sec = dt.quot;
    its.it_value.tv_nsec = dt.rem * 1000000;
    if (!periodic) {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
    }
    else {
        its.it_interval = its.it_value;
    }
    timerfd_settime(this->fd, 0, &its, NULL);
    ELLOCKLOOP(this->loop);
    auto so = this->loop->operations[this->fd].get();
    ELUNLOCKLOOP(this->loop);
    if (so)
        so->inCb = cb;
}

void SysTimer::stopTimer()
{
    itimerspec its;
    memset(&its, 0, sizeof(its));
    timerfd_settime(this->fd, 0, &its, NULL);
    ELLOCKLOOP(this->loop);
    auto so = this->loop->operations[this->fd].get();
    ELUNLOCKLOOP(this->loop);
    if (so)
        so->inCb = 0;
}

SysTimer::~SysTimer() {}

}
