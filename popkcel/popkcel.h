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
#pragma once

#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#    include <BaseTsd.h>
#    include <WinSock2.h>
#    include <ws2tcpip.h>

#include <windows.h>
//#include <setjmpex.h>
typedef SSIZE_T ssize_t;
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    ifdef __linux__
#        include <sys/epoll.h>
#    else
#        include <sys/time.h>
#        include <sys/types.h>

#        include <sys/event.h>
#    endif
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef LIBPOPKCEL_EXTERN
#    ifdef _WIN32
#        if defined(LIBPOPKCEL_SHARED)
#            define LIBPOPKCEL_EXTERN __declspec(dllexport)
#        elif defined(USING_LIBPOPKCEL_SHARED)
#            define LIBPOPKCEL_EXTERN __declspec(dllimport)
#        else
#            define LIBPOPKCEL_EXTERN
#        endif
#    elif __GNUC__ >= 4
#        define LIBPOPKCEL_EXTERN __attribute__((visibility("default")))
#    else
#        define LIBPOPKCEL_EXTERN
#    endif
#endif

#ifdef POPKCELNOLOCK
#    define ELLOCKLOOP(l) (void)0
#    define ELUNLOCKLOOP(l) (void)0
#else
#    define ELLOCKLOOP(l) (l)->lock()
#    define ELUNLOCKLOOP(l) (l)->unlock()
#endif

#ifndef STACKGROWTHUP
#    define CHECKIFONSTACK(v, s) assert((v < (void*)&sp || v > (void*)loop->stackPos) && s)
#else
#    define CHECKIFONSTACK(v, s) assert((v > (void*)&sp || v < (void*)loop->stackPos) && s)
#endif

#ifdef NDEBUG
#define ELCHECKIFONSTACK(v,s)
#else
#define ELCHECKIFONSTACK(v,s) volatile char sp;CHECKIFONSTACK(v,s)
#endif

#ifdef _MSC_VER
#ifdef _M_IX86
typedef int32_t PopkcJmpBuf[6];
#define POPKCJMPBUF PopkcJmpBuf
#else
typedef int64_t PopkcJmpBuf[30];
#define POPKCJMPBUF __declspec(align(16)) PopkcJmpBuf
#endif
extern "C" LIBPOPKCEL_EXTERN int popkcSetjmp(PopkcJmpBuf env);
extern "C" LIBPOPKCEL_EXTERN void popkcLongjmp(PopkcJmpBuf env,int value);
#define POPKCSETJMP popkcSetjmp
#define POPKCLONGJMP popkcLongjmp
#else
#define POPKCSETJMP setjmp
#define POPKCLONGJMP longjmp
#define POPKCJMPBUF jmp_buf
#endif

namespace popkcel {

struct Loop;
struct MultiOperation;
struct LoopPool;
struct Timer;

enum Event {
    EVENT_IN = 1,
    EVENT_OUT = 2,
    EVENT_DISCONNECTED = 4,
    EVENT_ERROR = 8
};

enum ContextState {
    CONTEXTSTATE_INIT,
    CONTEXTSTATE_SUSPENDED,
    CONTEXTSTATE_RESUMED
};

enum SockRv {
    SOCKRV_OK,
    SOCKRV_ERROR = -1,
    SOCKRV_WOULDBLOCK = -2,
    SOCKRV_PENDING = -3,
    SOCKRV_ACCEPTFAIL = -4
};

enum SocketType {
    SOCKETTYPE_TCP,
    SOCKETTYPE_UDP
};

#ifdef _WIN32
typedef HANDLE HandleType;
#elif (defined __linux__)
typedef int HandleType;
#else
typedef uintptr_t HandleType;
#endif

typedef std::function<void(intptr_t)> FuncCallback;
typedef std::function<void(int)> FuncRedo;
typedef std::function<void(HandleType fd, sockaddr* addr, socklen_t addrLen)> FuncAccept;
typedef std::multimap<std::chrono::steady_clock::time_point, Timer*> TimerMap;

struct LIBPOPKCEL_EXTERN Context
{
	POPKCJMPBUF jmpBuf;
    char* stackPos;
    char* savedStack = nullptr;

    ~Context();
};

enum SingleOperationType {
    SOT_NOACTION,
    SOT_READ,
    SOT_WRITE,
    SOT_RECVFROM,
    SOT_SENDTO
};

enum HandleTypeEnum {
    HT_UNKNOWN,
    HT_SOCKET,
    HT_NOTIFIER,
    HT_TIMER,
    HT_LISTENSOCKET
};

struct SingleOperation
{
    FuncCallback inCb;
    FuncCallback outCb;
    FuncRedo inRedo;
    FuncRedo outRedo;
};

struct LIBPOPKCEL_EXTERN Handle
{
#ifndef _WIN32
	SingleOperation* addToLoop();
#ifndef __linux__
    HandleTypeEnum handleTypeEnum;
#endif
#endif
    Loop* loop;
    HandleType fd;
    Handle(Loop* loop);
    ~Handle();
};

struct LIBPOPKCEL_EXTERN Timer
{
    FuncCallback funcCb;
    TimerMap::iterator iter;
    Loop* loop;
    int interval;
    Timer(Loop* loop);
    ~Timer();
    void setTimer(uint32_t timeout, uint32_t interval = 0);
    void stopTimer();
};

struct LIBPOPKCEL_EXTERN SysTimer : Handle
{
#ifdef _WIN32
	FuncCallback funcCb;
#endif
    SysTimer(Loop* loop);
    ~SysTimer();
    void setTimer(int timeout, const FuncCallback& cb, bool periodic = false);
    void stopTimer();
};

#ifdef _WIN32
LIBPOPKCEL_EXTERN std::string GetLastErrorAsString();
struct LIBPOPKCEL_EXTERN IocpCallback
{
	OVERLAPPED ol;
	FuncCallback funcCb;
	IocpCallback();
};
#endif

//Socket的函数，成功或失败时直接返回，如果是在异步处理中，则在操作结束后调用回调函数。
struct LIBPOPKCEL_EXTERN Socket : Handle
{
    int tryConnect(sockaddr* addr, socklen_t len, const FuncCallback& cb);
    ssize_t tryWrite(const char* buf, size_t len, const FuncCallback& cb);
    ssize_t trySendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, const FuncCallback& cb);
    ssize_t tryRead(char* buf, size_t len, const FuncCallback& cb);
    ssize_t tryRecvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, const FuncCallback& cb);
    Socket(Loop* loop, SocketType type);
    Socket(Loop* loop, HandleType fd);
#ifdef _WIN32
	IocpCallback *ic=nullptr;
	~Socket();
#endif // _WIN32

};

struct ReadInfo
{
    char* buf;
    size_t len;
};

struct LIBPOPKCEL_EXTERN PSSocket : Socket
{
    std::vector<ReadInfo*> readInfos;
    MultiOperation* mo = nullptr;
    PSSocket(Loop* loop, SocketType type);
    PSSocket(Loop* loop, HandleType fd);
    ~PSSocket();
    int connect(sockaddr* addr, int len, int timeout = -1);
    ssize_t write(const char* buf, size_t len, int timeout = -1);
    ssize_t sendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, int timeout = -1);
    ssize_t read(char* buf, size_t len, int timeout = -1);
    ssize_t readFor(char* buf, size_t len, int timeout = -1);
    ssize_t recvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, int timeout = -1);
    void multiConnect(sockaddr* addr, int len, MultiOperation* mo);
    void multiWrite(const char* buf, size_t len, MultiOperation* mo);
    void multiSendto(const char* buf, size_t len, sockaddr* addr, socklen_t addrLen, MultiOperation* mo);
    void multiRead(char* buf, size_t len, MultiOperation* mo);
    void multiReadFor(char* buf, size_t len, MultiOperation* mo);
    void multiRecvfrom(char* buf, size_t len, sockaddr* addr, socklen_t* addrLen, MultiOperation* mo);

protected:
    void generalCb(intptr_t rv);
    void readForCb(ReadInfo* ri, intptr_t rv);
};

struct LIBPOPKCEL_EXTERN Listener : Handle
{
    FuncAccept funcAccept;
#ifdef _WIN32
	char buffer[sizeof(sockaddr) * 2 + 32];
	HandleType curSock;
	~Listener();
	void acceptOne();
	void acceptCb(intptr_t rv);
#endif
    Listener(Loop* loop, HandleType fd = 0);
    int listen(uint16_t port, int backlog = SOMAXCONN);
};

struct LIBPOPKCEL_EXTERN MultiOperation
{
    std::map<PSSocket*, int> mapRv;
    Context context;
    Timer* timer = nullptr;
    Loop* loop;
    PSSocket* curSocket=nullptr;
    int count = 0;
    int multiCallback;
    bool timeOuted;

    MultiOperation(Loop* loop);
    ~MultiOperation();
    void wait(int timeout = -1, int multiCallback = 0);
    void reBlock() noexcept;
    void checkCount();
};

struct LIBPOPKCEL_EXTERN Notifier : Handle
{
    Notifier(Loop* loop, HandleType fd = 0);
    ~Notifier();
#ifndef _WIN32
    void inRedo(int ev);
#else
	FuncCallback funcCb;
#endif
    void setCb(const FuncCallback& cb);
    int notify();
};

/*对于linux和win采取完全不同的通知方法，linux记录redo函数，在相应的event产生时，调用redo函数处理，由redo函数在完成或错误时去通知用户。
 * win的话用带overlapped的结构体存放回调函数，当IO完成时，直接调用回调函数。
 */
struct LIBPOPKCEL_EXTERN Loop
{
    TimerMap timers;
    std::unordered_set<MultiOperation*> moSet;
	POPKCJMPBUF jmpBuf;
    Context* curContext;
    //当比原来的最短时间更短的timer出现时，需要一个timerFd通知loop
    SysTimer* sysTimer;
    char* stackPos;
    LoopPool* loopPool;
#ifdef _WIN32
	IocpCallback* curOverlapped=nullptr;
	ULONG_PTR completionKey;
	DWORD numOfBytes;
#else
    std::unordered_map<HandleType, std::shared_ptr<SingleOperation> > operations;
#    ifdef __linux__
    epoll_event* events;
#    else
    std::unordered_map<Handle*, FuncCallback> callbacks;
    struct kevent* events;
#    endif
    size_t maxEvents;
    //lock只是用来在线程间移动socket时保护operations这个map的，其他东西跨线程用不到也就无需保护
    std::atomic<bool> spinLock;
	int curIndex;
	int numOfEvents;
	bool inited;
    void lock();
    void unlock();
    int addHandle(Handle* handle);
    int removeHandle(Handle* handle);
	void eventCall(HandleType fd, int event);
#endif
    HandleType loopFd;
    bool running = false;

    Loop(LoopPool* loopPool = nullptr, size_t maxEvents = 16);
    ~Loop();
	Loop(const Loop&);
    Loop& operator=(const Loop&) = delete;
    void setupFirstTimeCb(const std::function<void()>& fft);
    void setTimeout(uint32_t timeout);
    void stopTimer();
    void removeMo(MultiOperation* mo);
    int checkTimers();

protected:
    void setupTimer();
};

struct LIBPOPKCEL_EXTERN LoopPool
{
#ifdef _WIN32
    HandleType iocp;
#endif
    std::vector<Loop> loops;
    std::vector<std::thread> threads;
    LoopPool(size_t numOfLoops = 0, size_t maxEvents = 16);
    int run();
    void detach();
    void closeAll();
    void moveSocket(size_t threadNum, Socket* socket);
};

extern thread_local Loop* threadLoop;

LIBPOPKCEL_EXTERN sockaddr_in address(uint32_t ip, uint16_t port);
LIBPOPKCEL_EXTERN sockaddr_in address(const char* ip, uint16_t port);
LIBPOPKCEL_EXTERN void suspend(Context* context) noexcept;
LIBPOPKCEL_EXTERN void resume(Context* context) noexcept;
LIBPOPKCEL_EXTERN int closeFd(HandleType fd);
LIBPOPKCEL_EXTERN int runLoop(Loop* loop);
LIBPOPKCEL_EXTERN int init();
}