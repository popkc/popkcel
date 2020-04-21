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
#include <ctime>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/event.h>
namespace popkcel {

namespace {
    int translateEvent(struct kevent* kev)
    {
        int flag;
        if (kev->filter == EVFILT_READ)
            flag = EVENT_IN;
        else
            flag = 0;
        if (kev->filter == EVFILT_WRITE)
            flag |= EVENT_OUT;

        if (kev->flags & EV_EOF)
            flag |= EVENT_DISCONNECTED;
        if (kev->flags & EV_ERROR)
            flag |= EVENT_ERROR;
        return flag;
    }

    void makeTimespec(timespec* ts, int ms)
    {
        div_t dt = div(ms, 1000);
        ts->tv_sec = dt.quot;
        ts->tv_nsec = dt.rem * 1000000;
    }
}

Loop::Loop(LoopPool* loopPool, size_t maxEvents)
    : spinLock(false)
{
    this->loopPool = loopPool;
    this->maxEvents = maxEvents;
    loopFd = kqueue();
    events = new struct kevent[maxEvents];
    setupTimer();
}

Loop::Loop(const Loop& loop)
    : spinLock(false)
{
    this->loopPool = loop.loopPool;
    this->maxEvents = loop.maxEvents;
    loopFd = kqueue();
    events = new struct kevent[maxEvents];
    setupTimer();
}

int Loop::addHandle(Handle* handle)
{
    struct kevent kev;
    int r;
    switch (handle->handleTypeEnum) {
    case HT_SOCKET:
        EV_SET(&kev, handle->fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
        r = kevent(loopFd, &kev, 1, NULL, 0, NULL);
        if (r < 0)
            return r;
        //故意不break
    case HT_LISTENSOCKET:
        EV_SET(&kev, handle->fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
        r = kevent(loopFd, &kev, 1, NULL, 0, NULL);
        break;
    case HT_NOTIFIER:
        EV_SET(&kev, (uintptr_t)handle, EVFILT_USER, EV_ADD | EV_CLEAR, NOTE_FFNOP, 0, NULL);
        r = kevent(loopFd, &kev, 1, NULL, 0, NULL);
        break;
    case HT_TIMER:
    default:
        break;
    }
    return r;
}

int Loop::removeHandle(Handle* handle)
{
    struct kevent kev;
    EV_SET(&kev, handle->fd, 0, EV_DELETE, 0, 0, NULL);
    return kevent(loopFd, &kev, 1, NULL, 0, NULL);
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
        if (r == -1) {
            loop->numOfEvents = kevent(loop->loopFd, NULL, 0, loop->events, loop->maxEvents, NULL);
        }
        else {
            timespec ts;
            makeTimespec(&ts, r);
            loop->numOfEvents = kevent(loop->loopFd, NULL, 0, loop->events, loop->maxEvents, &ts);
        }
        loop->curIndex = 0;
        if (!loop->inited) {
            loop->inited = true;
            if (setjmp(loop->jmpBuf)) {
                loop = threadLoop;
                loop->curIndex++;
            }
        }

        while (loop->curIndex < loop->numOfEvents) {
            auto& kev = loop->events[loop->curIndex];
            if (kev.filter == EVFILT_USER || kev.filter == EVFILT_TIMER) {
                auto it = loop->callbacks.find(reinterpret_cast<Handle*>(kev.ident));
                if (it != loop->callbacks.end())
                    it->second((kev.flags & (EV_EOF | EV_ERROR)) ? SOCKRV_ERROR : SOCKRV_OK);
            }
            else {
                int event = translateEvent(&kev);
                loop->eventCall(kev.ident, event);
            }
            loop->curIndex++;
        }
    } while (loop->running);
    return 0;
}

Notifier::Notifier(Loop* loop, HandleType fd)
    : Handle(loop)
{
    handleTypeEnum = HT_NOTIFIER;
    this->fd = 0;
    loop->addHandle(this);
}

int Notifier::notify()
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)this, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    return kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
}

void Notifier::setCb(const FuncCallback& cb)
{
    loop->callbacks[this] = cb;
}

Notifier::~Notifier()
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)this, EVFILT_USER, EV_DELETE, NOTE_FFNOP, 0, NULL);
    kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
    loop->callbacks.erase(this);
}

SysTimer::SysTimer(Loop* loop)
    : Handle(loop)
{
    this->handleTypeEnum = HT_TIMER;
    this->fd = 0;
}

void SysTimer::setTimer(int timeout, const FuncCallback& cb, bool periodic)
{
    struct kevent kev;
    uint16_t flag = EV_ADD | EV_CLEAR;
    if (!periodic)
        flag |= EV_ONESHOT;
    EV_SET(&kev, (uintptr_t)this, EVFILT_TIMER, flag, 0, timeout, NULL);
    if (kevent(loop->loopFd, &kev, 1, NULL, 0, NULL) < 0)
        return;
    loop->callbacks[this] = cb;
}

void SysTimer::stopTimer()
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)this, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
    loop->callbacks.erase(this);
}

SysTimer::~SysTimer()
{
    stopTimer();
}

}
