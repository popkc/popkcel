/*
Copyright (C) 2020-2023 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "popkcel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

static int translateEvent(int event)
{
    int tev = 0;
    if (event & EPOLLIN)
        tev |= POPKCEL_EVENT_IN;
    if (event & EPOLLOUT)
        tev |= POPKCEL_EVENT_OUT;
    if (event & EPOLLERR)
        tev |= POPKCEL_EVENT_ERROR;
    return tev;
}

int popkcel_addHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle, int evs)
{
    struct epoll_event ev;
    if (!evs)
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    else {
        ev.events = 0;
        if (evs & POPKCEL_EVENT_IN)
            ev.events |= EPOLLIN;
        if (evs & POPKCEL_EVENT_OUT)
            ev.events |= EPOLLOUT;
        if (evs & POPKCEL_EVENT_EDGE)
            ev.events |= EPOLLET;
    }
    ev.data.ptr = handle;
    return epoll_ctl(loop->loopFd, EPOLL_CTL_ADD, handle->fd, &ev);
}

int popkcel_removeHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle)
{
    return epoll_ctl(loop->loopFd, EPOLL_CTL_DEL, handle->fd, NULL);
}

int popkcel_runLoop(struct Popkcel_Loop* loop)
{
#ifndef POPKCEL_NOFAKESYNC
    volatile char stackPos;
    loop->stackPos = (char*)&stackPos;
#endif
    popkcel_threadLoop = loop;
    loop->inited = 0;
    loop->running = 1;
    for (;;) {
        int r = popkcel__checkTimers();
        /*
        printf("ct %d\n", r);
        fflush(stdout);
*/
        loop->numOfEvents = epoll_wait(loop->loopFd, loop->events, loop->maxEvents, r);
        /*
        printf("return %d\n", loop->numOfEvents);
        fflush(stdout);
        r = errno;
*/
        loop->curIndex = 0;
        if (!loop->inited) {
            loop->inited = 1;
#ifndef POPKCEL_NOFAKESYNC
            if (setjmp(loop->jmpBuf)) {
                loop = popkcel_threadLoop;
                loop->curIndex++;
            }
#endif
        }

        if (!loop->running)
            break;

        while (loop->curIndex < loop->numOfEvents) {
            struct Popkcel_SingleOperation* so = loop->events[loop->curIndex].data.ptr;
            int event = translateEvent(loop->events[loop->curIndex].events);
            popkcel__eventCall(so, event);
            loop->curIndex++;
        }
    }
    return 0;
}

static int sysTimerInRedo(void* data, intptr_t ev)
{
    struct Popkcel_SysTimer* sysTimer = data;
    char buf[8];
    int r = read(sysTimer->fd, buf, 8);
    if (r == 8) {
        if (sysTimer->so.inCb)
            return sysTimer->so.inCb(sysTimer->so.inCbData, (ev & POPKCEL_EVENT_ERROR) ? POPKCEL_ERROR : POPKCEL_OK);
    }
    return 0;
}

void popkcel_initSysTimer(struct Popkcel_SysTimer* sysTimer, struct Popkcel_Loop* loop)
{
    popkcel_initHandle((struct Popkcel_Handle*)sysTimer, loop);
    sysTimer->fd = timerfd_create(CLOCK_MONOTONIC, 0);
    sysTimer->so.inRedo = &sysTimerInRedo;
    sysTimer->so.inRedoData = sysTimer;
    int f = fcntl(sysTimer->fd, F_GETFL);
    fcntl(sysTimer->fd, F_SETFL, f | O_NONBLOCK);
    popkcel_addHandle(loop, (struct Popkcel_Handle*)sysTimer, POPKCEL_EVENT_IN | POPKCEL_EVENT_EDGE);
}

void popkcel_destroySysTimer(struct Popkcel_SysTimer* sysTimer)
{
    close(sysTimer->fd);
}

void popkcel_setSysTimer(struct Popkcel_SysTimer* sysTimer, unsigned int timeout, char periodic, Popkcel_FuncCallback cb, void* data)
{
    div_t dt = div(timeout, 1000);
    struct itimerspec its;
    its.it_value.tv_sec = dt.quot;
    its.it_value.tv_nsec = dt.rem * 1000000;
    if (!periodic) {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
    }
    else {
        its.it_interval = its.it_value;
    }
    sysTimer->so.inCb = cb;
    sysTimer->so.inCbData = data;
    timerfd_settime(sysTimer->fd, 0, &its, NULL);
}

void popkcel_stopSysTimer(struct Popkcel_SysTimer* sysTimer)
{
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    timerfd_settime(sysTimer->fd, 0, &its, NULL);
    sysTimer->so.inCb = NULL;
}

static int notifierInRedo(void* data, intptr_t ev)
{
    struct Popkcel_Notifier* nt = data;
    char buf[8];
    int r = read(nt->fd, buf, 8);
    if (r > 0) {
        if (nt->so.inCb)
            return nt->so.inCb(nt->so.inCbData, POPKCEL_OK);
    }
    return 0;
}

void popkcel_initNotifier(struct Popkcel_Notifier* notifier, struct Popkcel_Loop* loop)
{
    popkcel_initHandle((struct Popkcel_Handle*)notifier, loop);
    notifier->fd = eventfd(0, EFD_NONBLOCK);
    notifier->so.inRedo = &notifierInRedo;
    notifier->so.inRedoData = notifier;
    int f = fcntl(notifier->fd, F_GETFL);
    fcntl(notifier->fd, F_SETFL, f | O_NONBLOCK);
    popkcel_addHandle(loop, (struct Popkcel_Handle*)notifier, POPKCEL_EVENT_IN | POPKCEL_EVENT_EDGE);
}

void popkcel_destroyNotifier(struct Popkcel_Notifier* notifier)
{
    if (notifier->fd)
        close(notifier->fd);
}

void popkcel_notifierSetCb(struct Popkcel_Notifier* notifier, Popkcel_FuncCallback cb, void* data)
{
    notifier->so.inCb = cb;
    notifier->so.inCbData = data;
}

int popkcel_notifierNotify(struct Popkcel_Notifier* notifier)
{
    uint64_t ui = 1;
    int r = write(notifier->fd, &ui, 8);
    if (r >= 0)
        return r;
    if (errno == EWOULDBLOCK || errno == EAGAIN)
        return POPKCEL_WOULDBLOCK;
    else
        return POPKCEL_ERROR;
}

void popkcel_initLoop(struct Popkcel_Loop* loop, size_t maxEvents)
{
    loop->running = 0;
    loop->timers = NULL;
    popkcel_initSysTimer(&loop->sysTimer, loop);
    if (maxEvents == 0)
        maxEvents = 8;
    loop->events = malloc(sizeof(struct epoll_event) * maxEvents);
    loop->maxEvents = maxEvents;
    loop->loopFd = epoll_create(maxEvents);
}
