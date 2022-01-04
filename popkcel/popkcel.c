/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "popkcel.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

POPKCEL_THREADLOCAL struct Popkcel_Loop* popkcel_threadLoop;
struct Popkcel_GlobalVar* popkcel_globalVar;

uint32_t popkcel__rand()
{
    uint32_t r = popkcel_globalVar->seed * 48271 % 2147483647;
    popkcel_globalVar->seed = r;
    if (popkcel_getCurrentTime() & 1)
        r |= 0x80000000UL;
    return r;
}

int popkcel__checkTimers()
{
    popkcel_stopSysTimer(&popkcel_threadLoop->sysTimer);
    int rv = -1;
    int64_t ctp = popkcel_getCurrentTime();
    struct Popkcel_Rbtnode* it;
    /*
    it = popkcel_rbtBegin(popkcel_threadLoop->timers);
    printf("timerrbtree %ld", ctp);
    while (it) {
        printf(" %ld", it->key);
        it = popkcel_rbtNext(it);
    }
    printf("\n");
*/
    while ((it = popkcel_rbtBegin(popkcel_threadLoop->timers))) {
        if (it->key <= ctp) {
            struct Popkcel_Timer* timer = (struct Popkcel_Timer*)it;
            popkcel_rbtDelete(&popkcel_threadLoop->timers, (struct Popkcel_Rbtnode*)timer);
            timer->iter.isRed = 2;
            if (timer->funcCb) {
                timer->funcCb(timer->cbData, (intptr_t)&timer);
            }

            if (timer) {
                if (timer->interval > 0) {
                    int64_t nctp = ctp + timer->interval;
                    timer->iter.key = nctp;
                    popkcel_rbtMultiInsert(&popkcel_threadLoop->timers, (struct Popkcel_Rbtnode*)timer);
                }
            }
        }
        else {
            rv = (int)(it->key - ctp);
            break;
        }
    }
    return rv;
}

void popkcel_setTimer(struct Popkcel_Timer* timer, unsigned int timeout, unsigned int interval)
{
    popkcel_stopTimer(timer);
    int64_t ct = popkcel_getCurrentTime() + timeout;
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(timer->loop->timers);
    //printf("setTimer %ld %ld\n", ct, it ? it->key : 0);
    if (!it || it->key > ct) {
        popkcel_setSysTimer(&timer->loop->sysTimer, timeout, 0, &popkcel__invokeLoop, timer->loop);
    }
    timer->iter.key = ct;
    timer->interval = interval;
    popkcel_rbtMultiInsert(&timer->loop->timers, (struct Popkcel_Rbtnode*)timer);
}

void popkcel_stopTimer(struct Popkcel_Timer* timer)
{
    if (timer->iter.isRed != 2) {
        popkcel_rbtDelete(&timer->loop->timers, (struct Popkcel_Rbtnode*)timer);
        timer->iter.isRed = 2;
    }
}

void popkcel_initTimer(struct Popkcel_Timer* timer, struct Popkcel_Loop* loop)
{
    timer->loop = loop;
    timer->iter.isRed = 2;
}

struct sockaddr_in popkcel_addressI(uint32_t ip, uint16_t port)
{
    struct sockaddr_in addr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    return addr;
}

int popkcel_address(struct sockaddr_in* addr, const char* ip, uint16_t port)
{
    addr->sin_port = htons(port);
    addr->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &addr->sin_addr) == 1)
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

int popkcel_address6(struct sockaddr_in6* addr, const char* ip, uint16_t port)
{
    addr->sin6_port = htons(port);
    addr->sin6_family = AF_INET6;
    addr->sin6_flowinfo = 0;
    addr->sin6_scope_id = 0;
    if (inet_pton(AF_INET6, ip, &addr->sin6_addr) == 1)
        return POPKCEL_OK;
    else
        return POPKCEL_ERROR;
}

static int oneShotCb(void* data, intptr_t rv)
{
    struct Popkcel_OneShot* os = data;
    os->cb(os->data, rv);
    popkcel_destroyNotifier(data);
    free(os);
    return 1;
}

void popkcel_oneShotCallback(struct Popkcel_Loop* loop, Popkcel_FuncCallback cb, void* data)
{
    struct Popkcel_OneShot* os = malloc(sizeof(struct Popkcel_OneShot));
    popkcel_initNotifier(&os->notifier, loop);
    os->cb = cb;
    os->data = data;
    popkcel_notifierSetCb(&os->notifier, &oneShotCb, os);
    popkcel_notifierNotify(&os->notifier);
}

void popkcel_destroyContext(struct Popkcel_Context* context)
{
    if (context->savedStack)
        free(context->savedStack);
}

#ifdef _MSC_VER
#    define FORCENOTINLINE __declspec(noinline)
#else
#    define FORCENOTINLINE __attribute__((noinline))
#endif

/*linux下amd64架构，直接用局部变量的地址当stack pointer会出错，似乎保存的数据少了。别的平台不清楚，不管那么多，
我只要用下级函数获取stack pointer，就能把整个函数的stack都包括进去，代价是会多复制一些没用的数据，但在不用汇编的情况下这个恐怕是无法避免的。
我试过用alloca，复制的量比这个还多。*/
FORCENOTINLINE static void getStackPos(struct Popkcel_Context* context)
{
    volatile char a;
    context->stackPos = (char*)&a;
}

//因为获得stack pointer是在下级的函数getStackPos里，所以恢复时也得调用下级函数，再调用memcpy。
//直接调用memcpy的话memcpy本身的stack会被覆盖，导致崩溃。
FORCENOTINLINE static void doMemcpy(char* start, char* end)
{
    memcpy(start, popkcel_threadLoop->curContext->savedStack, end - start);
    //stack恢复后，返回地址已不可信，所以直接用longjmp回到上级函数中。
    POPKCLONGJMP(popkcel_threadLoop->curContext->jmpBuf, 2);
}

void popkcel_suspend(struct Popkcel_Context* context)
{
    assert(popkcel_threadLoop->running && "loop must be running!");
    switch (POPKCSETJMP(context->jmpBuf)) {
    case 1: {
        //resume后，会执行这里，此时context的值已不可信，应该用threadLoop->curContext。通常stack是向下扩展的，但这里也支持向上扩展的架构。
#ifndef STACKGROWTHUP
        //memcpy(threadLoop->curContext->stackPos, threadLoop->curContext->savedStack, threadLoop->stackPos - threadLoop->curContext->stackPos);
        doMemcpy(popkcel_threadLoop->curContext->stackPos, popkcel_threadLoop->stackPos);
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
        stackSize = popkcel_threadLoop->stackPos - context->stackPos;
        //printf("%d\n", stackSize);
        context->savedStack = malloc(stackSize);
        memcpy(context->savedStack, context->stackPos, stackSize);
#else
        //context->stackPos = (char*)sp;
        stackSize = context->stackPos - threadLoop->stackPos;
        context->savedStack = new char[stackSize];
        memcpy(context->savedStack, threadLoop->stackPos, stackSize);
#endif
        POPKCLONGJMP(popkcel_threadLoop->jmpBuf, 1);
    } break;
    default:
        break;
    }
}

void popkcel_resume(struct Popkcel_Context* context)
{
    popkcel_threadLoop->curContext = context;
    POPKCLONGJMP(context->jmpBuf, 1);
}

void popkcel_initMultiOperation(struct Popkcel_MultiOperation* mo, struct Popkcel_Loop* loop)
{
#ifndef NDEBUG
    if (loop->running) {
        ELCHECKIFONSTACK(loop, mo, "Do not allocate MultiOperation on stack!");
    }
#endif
    mo->loop = loop;
    mo->rvs = NULL;
    mo->count = 0;
    //popkcel_hashInsert(loop->moHash, loop->hashSize, (struct Popkcel_HashInfo*)mo);
    popkcel_initTimer(&mo->timer, loop);
    popkcel_initContext(&mo->context);
}

void popkcel_resetMultiOperation(struct Popkcel_MultiOperation* mo)
{
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(mo->rvs);
    while (it) {
        struct Popkcel_Rbtnode* it2 = it;
        it = popkcel_rbtNext(it);
        free(it2);
    }
    mo->rvs = NULL;
    mo->count = 0;
    popkcel_stopTimer(&mo->timer);
    popkcel_destroyContext(&mo->context);
    popkcel_initContext(&mo->context);
}

void popkcel_destroyMultiOperation(struct Popkcel_MultiOperation* mo)
{
    popkcel_resetMultiOperation(mo);
    //popkcel_hashRemove(mo->loop->moHash, mo->loop->hashSize, (struct Popkcel_HashInfo*)mo);
}

void popkcel_multiOperationReblock(struct Popkcel_MultiOperation* mo)
{
    if (mo->count <= 0 || mo->timeOuted)
        return;
    POPKCLONGJMP(mo->loop->jmpBuf, 1);
}

inline static void moCheckCount(struct Popkcel_MultiOperation* mo)
{
    if (mo->multiCallback || mo->count <= 0) {
        popkcel_resume(&mo->context);
    }
}

static int moGeneralCb(void* data, intptr_t rv)
{
    struct Popkcel_PSSocket* sock = data;
    struct Popkcel_RbtnodeData* it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(sock->mo->rvs, (int64_t)sock);
    it->value = (void*)rv;
    sock->mo->count--;
    sock->mo->curSocket = sock;
    moCheckCount(sock->mo);
    return 0;
}

static int moTimerCb(void* data, intptr_t rv)
{
    (void)rv;
    struct Popkcel_MultiOperation* mo = data;
    if (mo->count > 0) {
        mo->timeOuted = 1;
        mo->curSocket = NULL;
        popkcel_resume(&mo->context);
    }
    return 0;
}

void popkcel_multiOperationWait(struct Popkcel_MultiOperation* mo, int timeout, char multiCallback)
{
    mo->timeOuted = 0;
    mo->multiCallback = multiCallback;
    mo->curSocket = NULL;
    if (!mo->count)
        return;
    if (timeout > 0) {
        mo->timer.funcCb = &moTimerCb;
        mo->timer.cbData = mo;
        popkcel_setTimer(&mo->timer, timeout, 0);
    }
    popkcel_suspend(&mo->context);
}

int popkcel_initPSSocket(struct Popkcel_PSSocket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd)
{
#ifndef NDEBUG
    if (loop->running) {
        ELCHECKIFONSTACK(loop, sock, "Do not allocate PSSocket on stack!");
    }
#endif
    sock->mo = NULL;
    return popkcel_initSocket((struct Popkcel_Socket*)sock, loop, socketType, fd);
}

#define MULTICALL(f, to, ...)                                                                              \
    struct Popkcel_MultiOperation* mo = malloc(sizeof(struct Popkcel_MultiOperation));                     \
    popkcel_initMultiOperation(mo, sock->loop);                                                            \
    f(__VA_ARGS__, mo);                                                                                    \
    popkcel_multiOperationWait(mo, to, 0);                                                                 \
    struct Popkcel_RbtnodeData* it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(mo->rvs, (int64_t)sock); \
    intptr_t r = (intptr_t)it->value;                                                                      \
    popkcel_destroyMultiOperation(mo);                                                                     \
    free(mo);                                                                                              \
    return r;

int popkcel_connect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, int len, int timeout)
{
    MULTICALL(popkcel_multiConnect, timeout, sock, addr, len);
}

ssize_t popkcel_write(struct Popkcel_PSSocket* sock, const char* buf, size_t len, int timeout)
{
    MULTICALL(popkcel_multiWrite, timeout, sock, buf, len);
}

ssize_t popkcel_sendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, int timeout)
{
    MULTICALL(popkcel_multiSendto, timeout, sock, buf, len, addr, addrLen);
}

ssize_t popkcel_read(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout)
{
    MULTICALL(popkcel_multiRead, timeout, sock, buf, len);
}

ssize_t popkcel_readFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout)
{
    MULTICALL(popkcel_multiReadFor, timeout, sock, buf, len);
}

ssize_t popkcel_recvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, int timeout)
{
    MULTICALL(popkcel_multiRecvfrom, timeout, sock, buf, len, addr, addrLen);
}

static void insertMoRvs(struct Popkcel_MultiOperation* mo, struct Popkcel_PSSocket* sock, intptr_t r)
{
    struct Popkcel_RbtInsertPos ipos = popkcel_rbtInsertPos(&mo->rvs, (int64_t)sock);
    assert(ipos.ipos && "A socket can have only one operation in a MultiOperation!");
    struct Popkcel_RbtnodeData* it = malloc(sizeof(struct Popkcel_RbtnodeData));
    it->key = (int64_t)sock;
    it->value = (void*)r;
    popkcel_rbtInsertAtPos(&mo->rvs, ipos, (struct Popkcel_Rbtnode*)it);
}

void popkcel_multiConnect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo)
{
    intptr_t r = popkcel_tryConnect((struct Popkcel_Socket*)sock, addr, addrLen, &moGeneralCb, sock);
    insertMoRvs(mo, sock, r);

    if (r == POPKCEL_WOULDBLOCK) {
        mo->count++;
        sock->mo = mo;
    }
}

void popkcel_multiWrite(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct Popkcel_MultiOperation* mo)
{
    ssize_t r = popkcel_tryWrite((struct Popkcel_Socket*)sock, buf, len, &moGeneralCb, sock);
    insertMoRvs(mo, sock, r);

    if (r == POPKCEL_WOULDBLOCK)
        r = 0;
    if (r < 0 || (size_t)r >= len)
        return;

    mo->count++;
    sock->mo = mo;
}

void popkcel_multiSendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo)
{
    ssize_t r = popkcel_trySendto((struct Popkcel_Socket*)sock, buf, len, addr, addrLen, &moGeneralCb, sock);
    insertMoRvs(mo, sock, r);

    if (r == POPKCEL_WOULDBLOCK) {
        mo->count++;
        sock->mo = mo;
    }
}

void popkcel_multiRead(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo)
{
    ssize_t r = popkcel_tryRead((struct Popkcel_Socket*)sock, buf, len, &moGeneralCb, sock);
    insertMoRvs(mo, sock, r);

    if (r == POPKCEL_WOULDBLOCK) {
        mo->count++;
        sock->mo = mo;
    }
}

static int moReadForCb(void* data, intptr_t rv)
{
    struct Popkcel_PSSocket* sock = data;
    if (rv < 0 || (size_t)rv >= sock->rlen) {
        sock->totalRead += rv;
        moGeneralCb(data, sock->totalRead);
    }
    else if (rv == 0) {
    meet0:;
        moGeneralCb(data, sock->totalRead);
    }
    else {
        for (;;) {
            sock->rbuf += rv;
            sock->rlen -= rv;
            sock->totalRead += rv;
            ssize_t r = popkcel_tryRead((struct Popkcel_Socket*)sock, sock->rbuf, sock->rlen, &moReadForCb, sock);

            if (r == POPKCEL_WOULDBLOCK)
                break;
            if (r < 0 || (size_t)r >= sock->rlen) {
                sock->totalRead += r;
                moGeneralCb(data, sock->totalRead);
            }
            else
                goto meet0;
        }
    }
    return 0;
}

void popkcel_multiReadFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo)
{
    struct Popkcel_RbtnodeData* it = NULL;
    sock->totalRead = 0;
    for (;;) {
        ssize_t r = popkcel_tryRead((struct Popkcel_Socket*)sock, buf, len, &moReadForCb, sock);
        if (!it) {
            struct Popkcel_RbtInsertPos ipos = popkcel_rbtInsertPos(&mo->rvs, (int64_t)sock);
            assert(ipos.ipos && "A socket can have only one operation in a MultiOperation!");
            it = malloc(sizeof(struct Popkcel_RbtnodeData));
            it->key = (int64_t)sock;
            popkcel_rbtInsertAtPos(&mo->rvs, ipos, (struct Popkcel_Rbtnode*)it);
        }
        it->value = (void*)r;

        if (r == POPKCEL_WOULDBLOCK)
            break;
        else if (r < 0 || (size_t)r >= len) {
            return;
        }
        else {
            buf += r;
            len -= r;
            sock->totalRead += r;
        }
    }

    mo->count++;
    sock->mo = mo;
}

void popkcel_multiRecvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, struct Popkcel_MultiOperation* mo)
{
    ssize_t r = popkcel_tryRecvfrom((struct Popkcel_Socket*)sock, buf, len, addr, addrLen, &moGeneralCb, sock);
    insertMoRvs(mo, sock, r);

    if (r == POPKCEL_WOULDBLOCK) {
        mo->count++;
        sock->mo = mo;
    }
}

void popkcel_initLoopPool(struct Popkcel_LoopPool* loopPool, size_t loopSize, size_t maxEvents)
{
#ifdef POPKCEL_SINGLETHREAD
    loopSize = 1;
#else
    if (loopSize == 0) {
#    ifndef _SC_NPROCESSORS_ONLN
        loopSize = 1;
#    else
        loopSize = sysconf(_SC_NPROCESSORS_ONLN);
        if (loopSize < 1)
            loopSize = 1;
#    endif
    }
#endif // POPKCEL_SINGLETHREAD
    loopPool->loopSize = loopSize;
    loopPool->loops = malloc((sizeof(struct Popkcel_Loop) + sizeof(Popkcel_ThreadType)) * loopSize);
    loopPool->threads = (Popkcel_ThreadType*)((char*)loopPool->loops + sizeof(struct Popkcel_Loop) * loopSize);
    for (size_t i = 0; i < loopSize; i++) {
        popkcel_initLoop(&loopPool->loops[i], maxEvents);
    }
}

void popkcel_destroyLoopPool(struct Popkcel_LoopPool* loopPool)
{
    for (size_t i = 0; i < loopPool->loopSize; i++) {
        popkcel_destroyLoop(&loopPool->loops[i]);
    }
    free(loopPool->loops);
}

void popkcel_loopPoolDetach(struct Popkcel_LoopPool* loopPool)
{
#ifndef POPKCEL_SINGLETHREAD
    loopPool->isRun = 0;
    for (size_t i = 0; i < loopPool->loopSize; i++) {
        //pthread_create(&loopPool->threads[i], NULL, runLoopCaller, &loopPool->loops[i]);
        pthread_create(&loopPool->threads[i], NULL, (void* (*)(void*)) & popkcel_runLoop, &loopPool->loops[i]);
        pthread_detach(loopPool->threads[i]);
    }
#endif
}

int popkcel_loopPoolRun(struct Popkcel_LoopPool* loopPool)
{
    loopPool->isRun = 1;
#ifndef POPKCEL_SINGLETHREAD
    for (size_t i = 0; i < loopPool->loopSize - 1; i++) {
        //pthread_create(&loopPool->threads[i], NULL, runLoopCaller, &loopPool->loops[i + 1]);
        pthread_create(&loopPool->threads[i], NULL, (void* (*)(void*)) & popkcel_runLoop, &loopPool->loops[i + 1]);
        pthread_detach(loopPool->threads[i]);
    }
#endif
    return popkcel_runLoop(loopPool->loops);
}

void popkcel_moveSocket(struct Popkcel_LoopPool* loopPool, size_t threadNum, struct Popkcel_Socket* sock)
{
#ifndef POPKCEL_SINGLETHREAD
    popkcel_removeHandle(sock->loop, (struct Popkcel_Handle*)sock);
    struct Popkcel_Loop* nl = &loopPool->loops[threadNum];
    sock->loop = nl;
    popkcel_addHandle(nl, (struct Popkcel_Handle*)sock, 0);
#endif
}

intptr_t popkcel_multiOperationGetResult(struct Popkcel_MultiOperation* mo, struct Popkcel_PSSocket* sock)
{
    struct Popkcel_RbtnodeData* it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(mo->rvs, (int64_t)sock);
    if (!it)
        return POPKCEL_ERROR;
    else
        return (intptr_t)it->value;
}

int popkcel_bind(struct Popkcel_Socket* sock, uint16_t port)
{
    struct sockaddr_in6 addr;
    socklen_t addrLen;
    if (!sock->ipv6) {
        *(struct sockaddr_in*)&addr = popkcel_addressI(0, port);
        addrLen = sizeof(struct sockaddr_in);
    }
    else {
        memset(&addr, 0, sizeof(struct sockaddr_in6));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addrLen = sizeof(struct sockaddr_in6);
#ifndef _WIN32
        int on = 1;
        setsockopt(sock->fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
#endif
    }
    if (bind((intptr_t)sock->fd, (struct sockaddr*)&addr, addrLen))
        return POPKCEL_ERROR;
    return POPKCEL_OK;
}

void popkcel__globalInit()
{
    popkcel_globalVar = malloc(sizeof(struct Popkcel_GlobalVar));
    popkcel_globalVar->seed = (uint32_t)popkcel_getCurrentTime();
    popkcel_globalVar->tempAddr = popkcel_addressI(0x8080804, 1234);
    popkcel_address6(&popkcel_globalVar->tempAddr6, "2001:4860:4860::8844", 1234);
}
