/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "popkcel.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/event.h>

static int translateEvent(struct kevent* kev)
{
    int flag;
    if (kev->filter == EVFILT_READ)
        flag = POPKCEL_EVENT_IN;
    else if (kev->filter == EVFILT_WRITE)
        flag = POPKCEL_EVENT_OUT;
    else
        flag = 0;

    if (kev->flags & (EV_ERROR | EV_EOF))
        flag |= POPKCEL_EVENT_ERROR;
    return flag;
}

static void makeTimespec(struct timespec* ts, int ms)
{
    div_t dt = div(ms, 1000);
    ts->tv_sec = dt.quot;
    ts->tv_nsec = dt.rem * 1000000;
}

void popkcel_initLoop(struct Popkcel_Loop* loop, size_t maxEvents)
{
    loop->loopFd = kqueue();
    if (maxEvents == 0)
        maxEvents = 8;
    loop->events = malloc(sizeof(struct kevent) * maxEvents);
    loop->maxEvents = maxEvents;
    popkcel_initSysTimer(&loop->sysTimer, loop);
    loop->timers = NULL;
}

int popkcel_addHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle, int ev)
{
    struct kevent kev;
    int r;
    uint16_t flag;
    if (!ev) {
        flag = EV_ADD | EV_CLEAR;
        ev = POPKCEL_EVENT_OUT | POPKCEL_EVENT_IN;
    }
    else {
        if (ev & POPKCEL_EVENT_EDGE)
            flag = EV_ADD | EV_CLEAR;
        else
            flag = EV_ADD;
    }

    if (ev & POPKCEL_EVENT_OUT) {
        EV_SET(&kev, handle->fd, EVFILT_WRITE, flag, 0, 0, handle);
        r = kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
        if (r < 0)
            return r;
    }

    if (ev & POPKCEL_EVENT_USER) {
        EV_SET(&kev, (uintptr_t)handle, EVFILT_USER, flag, NOTE_FFNOP, 0, handle);
        r = kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
        if (r < 0)
            return r;
    }

    EV_SET(&kev, handle->fd, EVFILT_READ, flag, 0, 0, handle);
    r = kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
    return r;
}

int popkcel_removeHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle)
{
    struct kevent kev;
    EV_SET(&kev, handle->fd, 0, EV_DELETE, 0, 0, NULL);
    return kevent(loop->loopFd, &kev, 1, NULL, 0, NULL);
}

int popkcel_runLoop(struct Popkcel_Loop* loop)
{
    volatile char stackPos;
    loop->stackPos = (char*)&stackPos;
    popkcel_threadLoop = loop;
    loop->inited = 0;
    loop->running = 1;
    for (;;) {
        int r = popkcel__checkTimers();
        if (r == -1) {
            loop->numOfEvents = kevent(loop->loopFd, NULL, 0, loop->events, loop->maxEvents, NULL);
        }
        else {
            struct timespec ts;
            makeTimespec(&ts, r);
            loop->numOfEvents = kevent(loop->loopFd, NULL, 0, loop->events, loop->maxEvents, &ts);
        }
        //int er = errno;
        loop->curIndex = 0;
        if (!loop->inited) {
            loop->inited = 1;
            if (setjmp(loop->jmpBuf)) {
                loop = popkcel_threadLoop;
                loop->curIndex++;
            }
        }

        if (!loop->running)
            break;

        while (loop->curIndex < loop->numOfEvents) {
            struct kevent* kev = &loop->events[loop->curIndex];
            if (kev->filter == EVFILT_USER || kev->filter == EVFILT_TIMER) {
                struct Popkcel_Handle* handle = (struct Popkcel_Handle*)kev->ident;
                if (handle && handle->so.inRedo) {
                    handle->so.inRedo(handle->so.inRedoData, (kev->flags & (EV_EOF | EV_ERROR)) ? POPKCEL_ERROR : POPKCEL_OK);
                }
            }
            else {
                int event = translateEvent(kev);
                popkcel__eventCall(kev->udata, event);
            }
            loop->curIndex++;
        }
    }
    return 0;
}

void popkcel_initNotifier(struct Popkcel_Notifier* notifier, struct Popkcel_Loop* loop)
{
    popkcel_initHandle((struct Popkcel_Handle*)notifier, loop);
    popkcel_addHandle(loop, (struct Popkcel_Handle*)notifier, POPKCEL_EVENT_USER | POPKCEL_EVENT_EDGE);
}

int popkcel_notifierNotify(struct Popkcel_Notifier* notifier)
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)notifier, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    return kevent(notifier->loop->loopFd, &kev, 1, NULL, 0, NULL);
}

void popkcel_notifierSetCb(struct Popkcel_Notifier* notifier, Popkcel_FuncCallback cb, void* data)
{
    notifier->so.inRedo = cb;
    notifier->so.inRedoData = data;
}

void popkcel_destroyNotifier(struct Popkcel_Notifier* notifier)
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)notifier, EVFILT_USER, EV_DELETE, NOTE_FFNOP, 0, NULL);
    kevent(notifier->loop->loopFd, &kev, 1, NULL, 0, NULL);
}

void popkcel_initSysTimer(struct Popkcel_SysTimer* sysTimer, struct Popkcel_Loop* loop)
{
    popkcel_initHandle((struct Popkcel_Handle*)sysTimer, loop);
}

void popkcel_setSysTimer(struct Popkcel_SysTimer* sysTimer, unsigned int timeout, char periodic, Popkcel_FuncCallback cb, void* data)
{
    struct kevent kev;
    uint16_t flag = EV_ADD | EV_CLEAR;
    if (!periodic)
        flag |= EV_ONESHOT;
    EV_SET(&kev, (uintptr_t)sysTimer, EVFILT_TIMER, flag, 0, timeout, sysTimer);
    sysTimer->so.inRedo = cb;
    sysTimer->so.inRedoData = data;
    kevent(sysTimer->loop->loopFd, &kev, 1, NULL, 0, NULL);
}

void popkcel_stopSysTimer(struct Popkcel_SysTimer* sysTimer)
{
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)sysTimer, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(sysTimer->loop->loopFd, &kev, 1, NULL, 0, NULL);
    sysTimer->so.inRedo = NULL;
}

void popkcel_destroySysTimer(struct Popkcel_SysTimer* sysTimer)
{
    popkcel_stopSysTimer(sysTimer);
}
