/*
Copyright (C) 2020-2023 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifdef _WIN32
#    define POPKCEL_SINGLETHREAD
#endif

#include <assert.h>
#include <errno.h>
#include <iostream>
#include <popkcel.h>
#include <string.h>

using namespace std;

namespace {
Popkcel_Loop* loop;

char* buf;
#ifdef _WIN32
//from https://stackoverflow.com/a/17387176
static std::string errorToString(DWORD err)
{
    DWORD errorMessageID = err;
    if (errorMessageID == 0)
        return std::string(); //No error message has been recorded

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    //Free the buffer.
    LocalFree(messageBuffer);

    return message;
}

static std::string GetLastErrorAsString()
{
    return errorToString(::GetLastError());
}
#endif
/*
void acceptCb(void* data, HandleType fd, sockaddr* addr, socklen_t addrLen)
{
    cout << "new connection " << fd << endl;
    PSSocket* ps = new PSSocket(threadLoop(), SOCKETTYPE_EXIST, fd);
    int r;
    while ((r = ps->read(buf, 10)) > 0) {
        cout.write(buf, r);
        cout.flush();
    }
    cout << "ncerror " << r << " " << errno << endl;
}

int oneShotCb(void* data, intptr_t rv)
{
    cout << "one shot cb" << endl;
    PSSocket* ps = new PSSocket((Loop*)data); //也可以用threadLoop()来获得当前线程的Loop
    sockaddr_in addr;
    address(&addr, "127.0.0.1", 1234);
    int r = ps->connect((sockaddr*)&addr, sizeof(addr), 3000);
    cout << "connect result " << r << endl;
    ps->write("abc", 3);
    return 0;
}
*/
Popkcel_PsrField *pfc, *pfs;
int count = 0;

int pfTimer(void* data, intptr_t rv)
{
    Popkcel_Timer** timer = (Popkcel_Timer**)rv;
    Popkcel_PsrField* pf = (Popkcel_PsrField*)data;
    delete *timer;
    *timer = NULL;
    char text[20];
    sprintf(text, "%d", count);
    //cout << "count " << count << endl;
    count++;
    popkcel_psrTrySend(pf, text, strlen(text), NULL, NULL);
    return 0;
}

int pfCb(Popkcel_PsrField* data, intptr_t rv)
{
    Popkcel_PsrField* pf = (Popkcel_PsrField*)data;
    if (rv > 0) {
        cout << "s ";
        cout.write(pf->recvBuf, rv);
        cout << endl;
        Popkcel_Timer* timer = new Popkcel_Timer;
        popkcel_initTimer(timer, pf->sock->loop);
        timer->funcCb = &pfTimer;
        timer->cbData = pf;
        popkcel_setTimer(timer, 1000, 0);
    }
    else if (rv == POPKCEL_ERROR) {
        cout << "server error" << endl;
    }
    else
        cout << "server unknown" << endl;
    return 0;
}

struct Popkcel_PsrField* psrListenCb(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr)
{
    if (psr)
        return NULL;
    pfs = new Popkcel_PsrField;
    popkcel_psrAcceptOne(sock, pfs, &pfCb);
    return pfs;
}

int pfcCb(Popkcel_PsrField* data, intptr_t rv)
{
    Popkcel_PsrField* pf = (Popkcel_PsrField*)data;
    if (rv == POPKCEL_CONNECTED) {
        cout << "c connected" << endl;
        popkcel_psrTrySend(pf, "test", 4, NULL, NULL);
    }
    else if (rv > 0) {
        cout << "c ";
        cout.write(pf->recvBuf, rv);
        cout << endl;
        if (count % 15 != 5) {
            Popkcel_Timer* timer = new Popkcel_Timer;
            popkcel_initTimer(timer, pf->sock->loop);
            timer->funcCb = &pfTimer;
            timer->cbData = pf;
            popkcel_setTimer(timer, 1000, 0);
        }
        else {
            popkcel_destroyPsrField(pf);
            popkcel_initPsrField(pf->sock, pf, &pfcCb);
            popkcel_psrTryConnect(pf);
            return 1;
        }
    }
    else if (rv == POPKCEL_ERROR) {
        cout << "client error" << endl;
    }
    else
        cout << "client unknown" << endl;
    return 0;
}

int testTimer(void* data, intptr_t rv)
{
    cout << "timer" << endl;
    return 0;
}

int pfOsCb(void* data, intptr_t rv)
{
    Popkcel_PsrSocket* psr = new Popkcel_PsrSocket;
    if (popkcel_initPsrSocket(psr, loop, 0, 0, 55555, &psrListenCb, NULL, 1000) == POPKCEL_ERROR) {
        cout << "initPsrSocket error." << endl;
        return 0;
    }
    Popkcel_PsrSocket* psr2 = new Popkcel_PsrSocket;
    if (popkcel_initPsrSocket(psr2, loop, 0, 0, 0, NULL, NULL, 1000) == POPKCEL_ERROR) {
        cout << "initPsrSocket error2." << endl;
        return 0;
    }
    pfc = new Popkcel_PsrField;
    popkcel_initPsrField(psr2, pfc, &pfcCb);
    popkcel_address((sockaddr_in*)&pfc->remoteAddr, "127.0.0.1", 55555);
    pfc->addrLen = sizeof(sockaddr_in);
    popkcel_psrTryConnect(pfc);
    Popkcel_Timer* timer = new Popkcel_Timer;
    popkcel_initTimer(timer, loop);
    timer->funcCb = &testTimer;
    timer->cbData = NULL;
    popkcel_setTimer(timer, 1500, 2500);
    return 0;
}

int stCb(void* data, intptr_t rv)
{
    cout << "timer" << endl;
    Popkcel_SysTimer* st = (Popkcel_SysTimer*)data;
    popkcel_destroySysTimer(st);
    popkcel_initSysTimer(st, loop);
    popkcel_setSysTimer(st, 500, 0, &stCb, st);
    return 1;
}

int sysTimerOsCb(void* data, intptr_t rv)
{
    Popkcel_SysTimer* st = new Popkcel_SysTimer;
    popkcel_initSysTimer(st, loop);
    popkcel_setSysTimer(st, 1000, 0, &stCb, st);
    return 0;
}

int pfNeCb(Popkcel_PsrField* data, intptr_t rv)
{
    cout << "pfNeCb " << rv << endl;
    return 0;
}

int psrNonexistOsCb(void* data, intptr_t rv)
{
    Popkcel_PsrSocket* ps = new Popkcel_PsrSocket;
    if (popkcel_initPsrSocket(ps, loop, 0, 0, 0, &psrListenCb, NULL, 1000) == POPKCEL_ERROR) {
        cout << "initPsrSocket error." << endl;
        return 0;
    }
    pfc = new Popkcel_PsrField;
    popkcel_initPsrField(ps, pfc, &pfNeCb);
    popkcel_address((sockaddr_in*)&pfc->remoteAddr, "127.0.0.1", 40897);
    pfc->addrLen = sizeof(sockaddr_in);
    int r = popkcel_psrTryConnect(pfc);
    cout << "oscb " << r << endl;
    return 0;
}

void testOscb(Popkcel_FuncCallback cb)
{
    loop = new Popkcel_Loop;
    popkcel_initLoop(loop, 0);
    popkcel_oneShotCallback(loop, cb, NULL);
    popkcel_runLoop(loop);
}

void testRbt()
{
    Popkcel_Rbtnode* root = NULL;
    Popkcel_Rbtnode* a = new Popkcel_Rbtnode[100];
    for (int64_t i = 0; i < 100; i++) {
        a[i].key = 100 - i;
        popkcel_rbtMultiInsert(&root, a + i);
    }
    auto it = popkcel_rbtBegin(root);
    while (it) {
        cout << it->key << " ";
        it = popkcel_rbtNext(it);
    }
    cout << endl;
    for (int i = 0; i < 100; i++) {
        it = popkcel_rbtBegin(root);
        popkcel_rbtDelete(&root, it);
    }
    assert(!root);
    for (int64_t i = 0; i < 100; i++) {
        a[i].key = i;
        auto ipos = popkcel_rbtInsertPos(&root, a[i].key);
        popkcel_rbtInsertAtPos(&root, ipos, a + i);
    }
    it = popkcel_rbtBegin(root);
    while (it) {
        cout << it->key << " ";
        it = popkcel_rbtNext(it);
    }
    cout << endl;
    for (int i = 80; i >= 50; i--) {
        popkcel_rbtDelete(&root, a + i);
    }
    for (int i = 50; i <= 60; i++) {
        popkcel_rbtMultiInsert(&root, a + i);
    }
    it = popkcel_rbtBegin(root);
    while (it) {
        cout << it->key << " ";
        it = popkcel_rbtNext(it);
    }
    cout << endl;
    cout << "ok" << endl;
}
}

int main()
{
    popkcel_init();
    testOscb(&psrNonexistOsCb);
    //testRbt();
    //testOscb(&pfOsCb);
    //testOscb(&sysTimerOsCb);
    /*
    buf = new char[10];
    LoopPool lp(4);
    Listener lsn(lp.getLoops());
    //Loop loop;
    //Listener lsn(&loop);
    lsn.funcAccept = &acceptCb;
    lsn.funcAcceptData = &lsn;
    int r = lsn.listen(1234);
    cout << "listen " << r << " " << errno << endl;
    lp.getLoops()[0].oneShotCallback(&oneShotCb, lp.loops);
    //loop.oneShotCallback(&oneShotCb, &loop);
    lp.run();
    //loop.run();
*/
    return 0;
}
