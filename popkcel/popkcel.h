/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef POPKCEL_H
#define POPKCEL_H

/** @file popkcel.h
 *  popkcel纯C头文件
 */

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#    include <BaseTsd.h>
#    include <WinSock2.h>
#    include <ws2tcpip.h>

#    include <windows.h>
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

#ifndef POPKCEL_SINGLETHREAD
#    ifdef _WIN32
#        define POPKCEL_SINGLETHREAD
#    else
#        include <pthread.h>
#    endif
#endif

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

#ifndef STACKGROWTHUP
#    define ELCHECKIFONSTACK2(l, v, s) \
        if ((l)->running)              \
        assert(((char*)(v) < (char*)&sp || (char*)(v) > (char*)(l)->stackPos) && s)
#else
#    define ELCHECKIFONSTACK2(l, v, s) \
        if ((l)->running)              \
        assert(((char*)(v) > (char*)&sp || (char*)(v) < (char*)(l)->stackPos) && s)
#endif

#ifdef NDEBUG
#    define ELCHECKIFONSTACK(l, v, s)
#else
#    define ELCHECKIFONSTACK(l, v, s) \
        volatile char sp;             \
        ELCHECKIFONSTACK2(l, v, s)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#    define POPKCEL_THREADLOCAL __declspec(thread)
#    ifdef _M_IX86
typedef int32_t PopkcJmpBuf[6];
#    else
typedef int64_t PopkcJmpBuf[30];
#    endif
LIBPOPKCEL_EXTERN int popkcSetjmp(PopkcJmpBuf env);
LIBPOPKCEL_EXTERN void popkcLongjmp(PopkcJmpBuf env, int value);
#    define POPKCSETJMP popkcSetjmp
#    define POPKCLONGJMP popkcLongjmp
#    define POPKCJMPBUF PopkcJmpBuf
#else
#    define POPKCSETJMP setjmp
#    define POPKCLONGJMP longjmp
#    define POPKCJMPBUF jmp_buf
#    define POPKCEL_THREADLOCAL __thread
#endif
///回调函数类型，data是用户指定的数据，rv的含义参见相关函数的说明。
typedef int (*Popkcel_FuncCallback)(void* data, intptr_t rv);

#ifndef _WIN32
typedef int Popkcel_HandleType;
#    define POPKCEL_HANDLEFIELD            \
        struct Popkcel_SingleOperation so; \
        struct Popkcel_Loop* loop;         \
        Popkcel_HandleType fd;
#    define POPKCEL_SOCKETPF    \
        void* writeBuffer;      \
        struct sockaddr* raddr; \
        socklen_t* raddrLen;
#else
struct Popkcel_Socket;
struct Popkcel_Rbtnode;

struct Popkcel_IocpCallback
{
    OVERLAPPED ol;
    Popkcel_FuncCallback funcCb;
    void* cbData;
    struct Popkcel_Socket* sock;
    Popkcel_FuncCallback funcCb2;
    void* cbData2;
    struct Popkcel_IocpCallback* next;
};

typedef HANDLE Popkcel_HandleType;
#    define POPKCEL_HANDLEFIELD    \
        struct Popkcel_Loop* loop; \
        Popkcel_HandleType fd;

#    define POPKCEL_SOCKETPF \
        struct Popkcel_IocpCallback* ic;
#endif
///用于部分函数的返回值的enum，也用于callback第二个参数传入
enum Popkcel_Rv {
    POPKCEL_OK = 0,
    POPKCEL_ERROR = -1,
    POPKCEL_WOULDBLOCK = -2,
    POPKCEL_PENDING = -3,
    POPKCEL_CONNECTED = -4
};

///事件enum，可以用|合并多个事件
enum Popkcel_Event {
    ///in事件，包含有数据可读、有新的连接等待accept等
    POPKCEL_EVENT_IN = 1,
    ///out事件，表示socket变为可写状态，包含连接成功的情况
    POPKCEL_EVENT_OUT = 2,
    ///错误事件
    POPKCEL_EVENT_ERROR = 4,
    ///边缘触发，指只监测状态变化，不监测当前状态的事件
    POPKCEL_EVENT_EDGE = 8,
    ///内部使用，用户事件
    POPKCEL_EVENT_USER = 16
};

///socket类型，可以用|合并多个类型
enum Popkcel_SocketType {
    ///TCP类型，与UDP互斥
    POPKCEL_SOCKETTYPE_TCP = 1,
    ///UDP类型，与TCP互斥
    POPKCEL_SOCKETTYPE_UDP = 2,
    ///SOCKET使用IPV6
    POPKCEL_SOCKETTYPE_IPV6 = 4,
    ///使用已存在的SOCKET，与TCP、UDP互斥，可以和IPV6同时出现
    POPKCEL_SOCKETTYPE_EXIST = 8
};

///用于记录切换协程所需信息的结构体
struct Popkcel_Context
{
    ///setjmp/longjmp使用的数据
    POPKCJMPBUF jmpBuf;
    ///协程切换时的stack位置
    char* stackPos;
    ///保存协程切换时的stack内容
    char* savedStack;
};

///初始化Context结构体
///\param context 需要初始化的Context的指针
static inline void popkcel_initContext(struct Popkcel_Context* context)
{
    context->savedStack = NULL;
}

///销毁Context结构体，这不会从内存中删除该Context
///\param context 需要销毁的Context结构体
LIBPOPKCEL_EXTERN void popkcel_destroyContext(struct Popkcel_Context* context);

#define POPKCEL_RBTFIELD                           \
    int64_t key;                                   \
    struct Popkcel_Rbtnode *parent, *left, *right; \
    char isRed;
///红黑树节点
struct Popkcel_Rbtnode
{
    POPKCEL_RBTFIELD
};

struct Popkcel_RbtnodeData
{
    POPKCEL_RBTFIELD
    void* value;
};

///存储红黑树insert操作的中间数据
struct Popkcel_RbtInsertPos
{
    ///如果可以插入，则ipos非NULL。如果不能插入，则ipos为NULL
    struct Popkcel_Rbtnode** ipos;
    ///如果可以插入，则parent为可插入的位置所在的节点，为NULL代表是根节点。如果不能插入，则parent指向key重复的节点
    struct Popkcel_Rbtnode* parent;
};

/**查找红黑树是否存在指定的key
@param root 红黑树的根节点
@param key 要查找的key
@return 如果找到，则返回指向该节点的指针，如果没找到，则返回NULL
*/
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtFind(struct Popkcel_Rbtnode* root, int64_t key);
/**获得红黑树指定key可插入的位置。这通常用于插入到不允许重复的红黑树中。
 * @param root 红黑树的根节点的指针
 * @param key 要插入的节点的key
 * @return 插入操作的中间数据结构体
 * */
LIBPOPKCEL_EXTERN struct Popkcel_RbtInsertPos popkcel_rbtInsertPos(struct Popkcel_Rbtnode** root, int64_t key);
/**插入到红黑树的指定位置中。这通常用于插入到不允许重复的红黑树中，在调用popkcel_rbtInsertPos后，使用返回的中间数据结构体调用本函数。
 * @param root 红黑树的根节点的指针
 * @param ipos 插入操作的中间数据结构体
 * @param inode 要插入的节点
 */
LIBPOPKCEL_EXTERN void popkcel_rbtInsertAtPos(struct Popkcel_Rbtnode** root, struct Popkcel_RbtInsertPos ipos,
    struct Popkcel_Rbtnode* inode);
/**插入到可重复的红黑树中
 * @param root 红黑树的根节点的指针
 * @param inode 要插入的节点
 */
LIBPOPKCEL_EXTERN void popkcel_rbtMultiInsert(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* inode);
/**获得红黑树中，指定节点的下一个节点
 * @param node 指定的节点
 * @return 如果存在下一个节点，则返回指向下一个节点的指针。如果不存在下一个节点（指定的节点已经是最后一个节点），则返回NULL
 */
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtNext(struct Popkcel_Rbtnode* node);
/**返回红黑树的第一个节点
 * @param root 红黑树的根节点
 * @return 返回指向第一个节点的指针，如果红黑树是空的，则返回NULL
 */
LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtBegin(struct Popkcel_Rbtnode* root);
/**从红黑树中删除指定的节点
 * @param root 红黑树的根节点的指针
 * @param node 要删除的节点。
 */
LIBPOPKCEL_EXTERN void popkcel_rbtDelete(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node);

LIBPOPKCEL_EXTERN struct Popkcel_Rbtnode* popkcel_rbtLowerBound(struct Popkcel_Rbtnode* root, int64_t key);

///记录事件的回调函数及传入回调函数的数据，仅在UNIX下使用。这是实现细节，无需关心。
struct Popkcel_SingleOperation
{
    ///in事件处理函数
    Popkcel_FuncCallback inRedo;
    ///out事件处理函数
    Popkcel_FuncCallback outRedo;
    ///in事件回调函数
    Popkcel_FuncCallback inCb;
    ///out事件回调函数
    Popkcel_FuncCallback outCb;
    ///指向链表的下一个数据
    struct Popkcel_SingleOperation* next;
    ///传入in事件处理函数的数据
    void* inRedoData;
    ///传入out事件处理函数的数据
    void* outRedoData;
    ///传入in事件回调函数的数据
    void* inCbData;
    ///传入out事件回调函数的数据
    void* outCbData;
};

/**关闭文件描述符
 * @param fd 要关闭的文件描述符
 * @return 成功返回POPKCEL_OK，失败返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_close(Popkcel_HandleType fd);

/**关闭socket
 * @param fd 要关闭的文件描述符
 * @return 成功返回POPKCEL_OK，失败返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_closeSocket(Popkcel_HandleType fd);

struct Popkcel_Loop;

///Handle类型，并没有被实际使用，它是其它Handle类型的“基类”
struct Popkcel_Handle
{
    POPKCEL_HANDLEFIELD
};

/**初始化Handle结构体
 * @param handle 要初始化的Handle
 * @param loop 将Handle初始化到这个Loop上
 */
LIBPOPKCEL_EXTERN void popkcel_initHandle(struct Popkcel_Handle* handle, struct Popkcel_Loop* loop);

///定时器类型。这个定时器是在popkcel库内完成的，并没有使用系统的Timer，这是因为有些系统的Timer数量有上限。不用系统Timer也会提高效率。
struct Popkcel_Timer
{
    struct Popkcel_Rbtnode iter;
    ///此Timer所属的Loop
    struct Popkcel_Loop* loop;
    ///Timer是存在于所属Loop的一个红黑树里，iter是Timer在那个红黑树里的节点的指针
    ///定时器触发时会运行的回调函数
    Popkcel_FuncCallback funcCb;
    ///传入回调函数的用户数据
    void* cbData;
    ///如果此值大于0，则Timer会间隔interval毫秒重复触发
    unsigned int interval;
};

/**初始化Timer
 * @param timer 需要初始化的Timer
 * @param loop 将Timer初始化到这个Loop上
 */
LIBPOPKCEL_EXTERN void popkcel_initTimer(struct Popkcel_Timer* timer, struct Popkcel_Loop* loop);
/**启动Timer。注意在此函数调用之前或之后，要设置好Timer的funcCb和cbData成员
 * @param timer 需要启动的Timer
 * @param timeout 第一次启动的延时，单位为毫秒
 * @param interval 如果此值大于0，则Timer会间隔interval毫秒重复触发
 */
LIBPOPKCEL_EXTERN void popkcel_setTimer(struct Popkcel_Timer* timer, unsigned int timeout, unsigned int interval);
/**停止Timer。Timer没有销毁函数，需要销毁时执行此函数即可
 * @param timer 要停止的timer
 */
LIBPOPKCEL_EXTERN void popkcel_stopTimer(struct Popkcel_Timer* timer);

///系统Timer。仅仅使用popkcel自行实现的Timer是不够的，有时需要用系统Timer来唤醒正在运行的loop。通常用户不需要使用SysTimer，用Timer类型就好了
struct Popkcel_SysTimer
{
    POPKCEL_HANDLEFIELD
    ///定时器触发时会运行的回调函数
    Popkcel_FuncCallback funcCb;
    ///传入回调函数的用户数据
    void* cbData;
};

/**初始化SysTimer
 * @param sysTimer 需要初始化的SysTimer
 * @param loop 将SysTimer初始化到这个Loop上
 */
LIBPOPKCEL_EXTERN void popkcel_initSysTimer(struct Popkcel_SysTimer* sysTimer, struct Popkcel_Loop* loop);
/**销毁SysTimer，这不会从内存中删除SysTimer
 * @param sysTimer 要销毁的SysTimer
 */
LIBPOPKCEL_EXTERN void popkcel_destroySysTimer(struct Popkcel_SysTimer* sysTimer);
/**启动SysTimer
 * @param sysTimer 要启动的SysTimer
 * @param timeout SysTimer第一次触发前的延时
 * @param periodic 是否为周期性触发。为0表示是一次性触发，非0表示是周期性触发。
 * @param cb SysTimer触发时会调用的回调函数
 * @param data 传入回调函数的用户数据
 */
LIBPOPKCEL_EXTERN void popkcel_setSysTimer(struct Popkcel_SysTimer* sysTimer, unsigned int timeout, char periodic, Popkcel_FuncCallback cb, void* data);
/**停止SysTimer
 * @param sysTimer 要停止的sysTimer
 */
LIBPOPKCEL_EXTERN void popkcel_stopSysTimer(struct Popkcel_SysTimer* sysTimer);
///内部使用的函数。用于在loop的sysTimer触发时唤醒loop
int popkcel__invokeLoop(void* data, intptr_t rv);

///通知者类型。用于用户手动触发的事件。
struct Popkcel_Notifier
{
    POPKCEL_HANDLEFIELD
#ifdef _WIN32
    Popkcel_FuncCallback funcCb;
    void* cbData;
#endif
};

/**初始化Notifier
 * @param notifier 需要初始化的Notifier
 * @param loop 将Notifier初始化到这个Loop上
 */
LIBPOPKCEL_EXTERN void popkcel_initNotifier(struct Popkcel_Notifier* notifier, struct Popkcel_Loop* loop);
/**销毁Notifier，这不会将Notifier从内存中删除
 * @param notifier 要销毁的notifier
 */
LIBPOPKCEL_EXTERN void popkcel_destroyNotifier(struct Popkcel_Notifier* notifier);
/**设置Notifier的回调函数
 * @param notifier 要设置的Notifier
 * @param cb 要设置的回调函数
 * @param data 传入回调函数的用户数据
 */
LIBPOPKCEL_EXTERN void popkcel_notifierSetCb(struct Popkcel_Notifier* notifier, Popkcel_FuncCallback cb, void* data);
/**触发Notifier，这将唤醒loop，并执行对应的回调函数
 * @param notifier 要触发的Notifier
 * @return 返回值大于等于0时，表示触发成功。小于0时，表示触发失败。
 */
LIBPOPKCEL_EXTERN int popkcel_notifierNotify(struct Popkcel_Notifier* notifier);

#define POPKCEL_SOCKETCOMMONFIELD \
    char ipv6;
#define POPKCEL_SOCKETFIELD \
    char* rbuf;             \
    size_t rlen;            \
    POPKCEL_SOCKETPF

///Socket类型。所有相关的函数都是用于进行异步SOCKET操作的
struct Popkcel_Socket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETCOMMONFIELD
    POPKCEL_SOCKETFIELD
};

/**初始化Socket
 * @param sock 要初始化的Socket
 * @param loop 将Socket初始化到这个Loop上
 * @param socketType 用于指定Socket是TCP还是UDP，是否IPV6，是使用已存在的Socket还是新建一个，具体见Popkcel_SocketType enum
 * @param fd 如果socketType包含POPKCEL_SOCKETTYPE_EXIST，则fd指定要使用的文件描述符。否则，此参数将被忽略。
 * @return 初始化成功则返回POPKCEL_OK，否则返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_initSocket(struct Popkcel_Socket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd);
/**销毁Socket，这不会将Socket从内存中删除
 * @param sock 要销毁的Socket
 */
LIBPOPKCEL_EXTERN void popkcel_destroySocket(struct Popkcel_Socket* sock);
/**将Socket绑定到指定的端口上。
 * @param sock 要绑定的Socket
 * @param port 要绑定到的端口号
 * @return 绑定成功则返回POPKCEL_OK，否则返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_bind(struct Popkcel_Socket* sock, uint16_t port);
/**发起异步连接，无论连接是否成功，它都会立即返回。
 * 
 * 如果连接立即成功或失败，则回调函数不会被调用。如果连接结果需要异步回调，那么会在连接完成时（成功或失败）执行回调函数。
 * 
 * 回调函数的第二个参数传入POPKCEL_OK表示连接成功，否则表示连接失败。
 * @param sock 连接使用的Socket
 * @param addr 要连接的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param cb 操作完成时回调的函数
 * @param data 传入回调函数的用户数据
 * @return 返回POPKCEL_OK表示立即成功。返回POPKCEL_ERROR表示立即失败。返回POPKCEL_WOULDBLOCK表示结果需要异步回调。
 */
LIBPOPKCEL_EXTERN int popkcel_tryConnect(struct Popkcel_Socket* sock, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data);
/**写入数据，通常用于发送TCP数据，无论写入是否成功，它都会立即返回。
 * 
 * 如果写入立即成功或失败，则回调函数不会被调用。如果写入结果需要异步回调，那么会在写入完成时（成功或失败）执行回调函数。
 * 
 * 回调函数的第二个参数是非负值表示写入成功，且该值为写入的字节数，若为负值表示写入失败。
 * @param sock 使用的Socket
 * @param buf 要发送的数据
 * @param len 要发送的数据的长度
 * @param cb 操作完成时回调的函数
 * @param data 传入回调函数的用户数据
 * @return 返回非负值表示立即成功，且该值为写入的字节数。返回POPKCEL_ERROR表示立即失败。返回POPKCEL_WOULDBLOCK表示结果需要异步回调。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_tryWrite(struct Popkcel_Socket* sock, const char* buf, size_t len, Popkcel_FuncCallback cb, void* data);
/**发送数据到指定地址，通常用于发送UDP数据，无论发送是否成功，它都会立即返回。
 * 
 * 如果发送立即成功或失败，则回调函数不会被调用。如果发送结果需要异步回调，那么会在发送完成时（成功或失败）执行回调函数。
 * 
 * 回调函数的第二个参数是非负值表示发送成功，且该值为写入的字节数，若为负值表示发送失败。
 * @param sock 使用的Socket
 * @param buf 要发送的数据
 * @param len 要发送的数据的长度
 * @param addr 要发送到的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param cb 操作完成时回调的函数
 * @param data 传入回调函数的用户数据
 * @return 返回非负值表示立即成功，且该值为写入的字节数。返回POPKCEL_ERROR表示立即失败。返回POPKCEL_WOULDBLOCK表示结果需要异步回调。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_trySendto(struct Popkcel_Socket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, Popkcel_FuncCallback cb, void* data);
/**读取数据，通常用于读取TCP数据，无论读取是否成功，它都会立即返回。
 * 
 * 如果读取立即成功或失败，则回调函数不会被调用。如果读取结果需要异步回调，那么会在发送完成时（成功或失败）执行回调函数。
 * 
 * 回调函数的第二个参数是正数表示读取成功，且该值为读取的字节数。若为负值表示读取失败。若为0表示读取结束，通常表明连接已断开。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区
 * @param len 缓冲区的大小
 * @param cb 操作完成时回调的函数
 * @param data 传入回调函数的用户数据
 * @return 返回正数表示立即成功，且该值为读取的字节数。返回POPKCEL_ERROR表示立即失败。返回POPKCEL_WOULDBLOCK表示结果需要异步回调。返回0表示读取结束，通常表明连接已断开。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_tryRead(struct Popkcel_Socket* sock, char* buf, size_t len, Popkcel_FuncCallback cb, void* data);
/**读取数据及其来源地址，通常用于读取UDP数据，无论读取是否成功，它都会立即返回。
 * 
 * 如果读取立即成功或失败，则回调函数不会被调用。如果读取结果需要异步回调，那么会在发送完成时（成功或失败）执行回调函数。
 * 
 * 回调函数的第二个参数是正数表示读取成功，且该值为读取的字节数。若为负值表示读取失败。若为0表示读取结束，通常表明连接已断开。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区
 * @param len 缓冲区的大小
 * @param addr [out]存放数据来源地址信息的结构体。如果是IPV4，建议用sockaddr_in类型。如果是IPV6，建议用sockaddr_in6类型
 * @param addrLen [out]addr结构体所占的字节数
 * @param cb 操作完成时回调的函数
 * @param data 传入回调函数的用户数据
 * @return 返回正数表示立即成功，且该值为读取的字节数。返回POPKCEL_ERROR表示立即失败。返回POPKCEL_WOULDBLOCK表示结果需要异步回调。返回0表示读取结束，通常表明连接已断开。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_tryRecvfrom(struct Popkcel_Socket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, Popkcel_FuncCallback cb, void* data);

#define POPKCEL_PSRVERSION 0

enum Popkcel_PsrState {
    POPKCEL_PS_INIT,
    POPKCEL_PS_CONNECTING,
    POPKCEL_PS_CONNECTED,
    POPKCEL_PS_TRANSFER,
    POPKCEL_PS_CLOSED
};

enum Popkcel_PsrFlag {
    POPKCEL_PF_SINGLE = 1,
    POPKCEL_PF_TRANSFORM = 1 << 1,
    POPKCEL_PF_REPLY = 1 << 2,
    POPKCEL_PF_CLOSED = 1 << 3,
    POPKCEL_PF_SYN = 1 << 4,
    POPKCEL_PF_CONFIRM = 1 << 5,
    POPKCEL_PF_APT = 1 << 7
};

struct Popkcel_RbtnodeBuf
{
    POPKCEL_RBTFIELD
    size_t bufLen;
    char buffer[];
};

struct Popkcel_PsrSocket;
struct Popkcel_PsrField;

struct Popkcel_RbtnodePsrSend
{
    POPKCEL_RBTFIELD
    struct Popkcel_Timer timer;
    struct Popkcel_PsrField* psr;
    Popkcel_FuncCallback callback;
    void* userData;
    size_t bufLen;
    int sendCount;
    char pending;
    char buffer[];
};

#define POPKCEL_MAXUDPSIZE 1472

struct Popkcel_PsrField;

///return 0表示继续，非0表示不要继续
typedef int (*Popkcel_PsrRecvCb)(struct Popkcel_PsrSocket* sock, intptr_t rv);
typedef struct Popkcel_PsrField* (*Popkcel_PsrListenCb)(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr);
typedef int (*Popkcel_PsrFuncCallback)(struct Popkcel_PsrField* psr, intptr_t rv);

///UDP Socket的PSRUDP协议（Popkc's Simple Reliable UDP）部分，可以把udp当成类似tcp来用，主要用于为内网IP之间提供连通能力和可靠传输
struct Popkcel_PsrField
{
    struct Popkcel_Timer timer;
    struct Popkcel_PsrSocket* sock;
    ///rbtree 发送了但还没收到回复确认的数据
    struct Popkcel_Rbtnode* nodePieceSend;
    ///rbtree 接收了的数据，但之前一些序号的数据还没收到
    struct Popkcel_Rbtnode* nodePieceReceive;
    struct Popkcel_Rbtnode* nodeReply;
    ///对方的IP、端口信息
    struct sockaddr_in6 remoteAddr;
    char buffer[POPKCEL_MAXUDPSIZE];
    char* recvBuf;
    ///回调函数
    Popkcel_PsrFuncCallback callback;
    Popkcel_FuncCallback lastSendCallback;
    void* userData;
    void* lastSendUserData;
    ///remoteAddr实际内容的长度
    socklen_t addrLen;
    uint32_t bufferPos;
    ///本机当前的发送序号
    uint32_t mySendId;
    ///对方当前的发送序号
    uint32_t othersSendId;
    uint32_t lastMyConfirmedSendId;
    ///防止传输被除中间方外的第三方伪造的id
    char tranId[4];
    char tranIdNew[4];
    ///此连接的窗口数
    uint16_t window;
    ///psrudp连接状态
    char state;
    char needSend;
    char timerStarted;
    char synConfirm;
};

#define POPKCEL_PSRSOCKETFIELD           \
    char psrBuffer[POPKCEL_MAXUDPSIZE];  \
    struct Popkcel_Timer timerKeepAlive; \
    int64_t lastSendTime;                \
    Popkcel_PsrListenCb listenCb;        \
    Popkcel_PsrRecvCb recvCb;            \
    struct sockaddr_in6 remoteAddr;      \
    struct Popkcel_Rbtnode* nodePf;      \
    void* userData;                      \
    socklen_t remoteAddrLen;             \
    char tranId[4];                      \
    uint32_t recvLen;                    \
    uint16_t maxWindow;                  \
    uint16_t psrWindow;

struct Popkcel_PsrSocket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETCOMMONFIELD
    POPKCEL_SOCKETFIELD
    POPKCEL_PSRSOCKETFIELD
};

LIBPOPKCEL_EXTERN int popkcel_initPsrSocket(struct Popkcel_PsrSocket* sock, struct Popkcel_Loop* loop, Popkcel_HandleType fd, char ipv6, uint16_t port, Popkcel_PsrListenCb listenCb, Popkcel_PsrRecvCb recvCb, uint16_t maxWindow);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrSocket(struct Popkcel_PsrSocket* sock);

LIBPOPKCEL_EXTERN void popkcel_initPsrField(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc);

LIBPOPKCEL_EXTERN void popkcel_destroyPsrField(struct Popkcel_PsrField* psr);

LIBPOPKCEL_EXTERN int popkcel_psrTryConnect(struct Popkcel_PsrField* psr);

LIBPOPKCEL_EXTERN int popkcel_psrTrySend(struct Popkcel_PsrField* psr, const char* data, size_t len, Popkcel_FuncCallback callback, void* userData);

LIBPOPKCEL_EXTERN void popkcel_psrAcceptOne(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc);

LIBPOPKCEL_EXTERN int popkcel_psrSendCache(struct Popkcel_PsrField* psr);

///MultiOperation用于在同一协程中同时对多个PSSocket执行伪同步操作。注意，MultiOperation一定要分配在heap上，不能分配在stack上。
struct Popkcel_MultiOperation
{
    ///与协程调度相关的数据
    struct Popkcel_Context context;
    ///定时器，如果有设定timeout的话就会用到
    struct Popkcel_Timer timer;
    ///存放各Socket操作结果的红黑树
    struct Popkcel_Rbtnode* rvs;
    ///与MultiOperation关联的Loop
    struct Popkcel_Loop* loop;
    ///在多次回调模式中，与本次的回调相关的socket
    struct Popkcel_PSSocket* curSocket;
    ///仍未完成异步操作的Socket的数量
    int count;
    ///是否为多次回调模式
    char multiCallback;
    ///是否已超时
    char timeOuted;
};

/**初始化MultiOperation
 * @param mo 要初始化的MultiOperation
 * @param loop 将MultiOperation初始化到这个Loop上
 */
LIBPOPKCEL_EXTERN void popkcel_initMultiOperation(struct Popkcel_MultiOperation* mo, struct Popkcel_Loop* loop);
/**销毁MultiOperation，这不会将MultiOperation从内存中删除。
 * @param mo 要销毁的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_destroyMultiOperation(struct Popkcel_MultiOperation* mo);
/**重置MultiOperation，以便重复使用。
 * 
 * 当你完成一次MultiOperation操作时，下次还想再用同一个MultiOperation的话，必须先执行这个函数进行重置。
 * @param mo 要重置的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_resetMultiOperation(struct Popkcel_MultiOperation* mo);
/**挂起MultiOperation，等待所有操作结束或者超时。如果没有需要异步完成的操作，那么本函数会立即返回。
 * @param mo 要挂起的MultiOperation
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @param multiCallback 非0表示是多次回调模式。
 */
LIBPOPKCEL_EXTERN void popkcel_multiOperationWait(struct Popkcel_MultiOperation* mo, int timeout, char multiCallback);
/**在多次回调模式中，用于重新使协程在popkcel_multiOperationWait处挂起。如果所有操作都已完成（成功或失败），或者超时了，那么本函数会立即返回。
 * @param mo 需要重新挂起的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiOperationReblock(struct Popkcel_MultiOperation* mo);
/**获得本次MultiOperation中，相应Socket的操作结果
 * @param mo 指定的MultiOperation
 * @param sock 要查询结果的Socket
 * @return 本次操作的结果。具体含义见相应操作的说明。
 */
LIBPOPKCEL_EXTERN intptr_t popkcel_multiOperationGetResult(struct Popkcel_MultiOperation* mo, struct Popkcel_PSSocket* sock);

#define POPKCEL_PSSOCKETFIELD          \
    struct Popkcel_MultiOperation* mo; \
    size_t totalRead;

///伪同步Socket，可以执行一些语法像同步、但实际是异步的操作。它是Socket的“派生类”，也能执行与Socket相关的函数。注意，PSSokcet一定要分配在heap上，不能分配在stack上。
struct Popkcel_PSSocket
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETCOMMONFIELD
    POPKCEL_SOCKETFIELD
    POPKCEL_PSSOCKETFIELD
};

/**销毁PSSocket
 * @param sock 要销毁的PSSocket
 */
static inline void popkcel_destroyPSSocket(struct Popkcel_PSSocket* sock)
{
    popkcel_destroySocket((struct Popkcel_Socket*)sock);
}

/**初始化PSSocket
 * @param sock 要初始化的PSSocket
 * @param loop 将PSSocket初始化到这个Loop上
 * @param socketType 用于指定Socket是TCP还是UDP，是否IPV6，是使用已存在的Socket还是新建一个，具体见Popkcel_SocketType enum
 * @param fd 如果socketType包含POPKCEL_SOCKETTYPE_EXIST，则fd指定要使用的文件描述符。否则，此参数将被忽略。
 * @return 初始化成功则返回POPKCEL_OK，否则返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_initPSSocket(struct Popkcel_PSSocket* sock, struct Popkcel_Loop* loop, int socketType, Popkcel_HandleType fd);
/**发起多操作下的伪同步连接，此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为POPKCEL_OK表示连接成功，为POPKCEL_ERROR表示显式地失败，为POPKCEL_WOULDBLOCK表示超时。
 * @param sock 连接使用的Socket
 * @param addr 要连接的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiConnect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo);
/**发起伪同步连接，此函数会挂起协程。
 * @param sock 连接使用的Socket
 * @param addr 要连接的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为POPKCEL_OK表示连接成功，为POPKCEL_ERROR表示显式地失败，为POPKCEL_WOULDBLOCK表示超时。
 */
LIBPOPKCEL_EXTERN int popkcel_connect(struct Popkcel_PSSocket* sock, struct sockaddr* addr, int len, int timeout);
/**发起多操作下的伪同步写入，通常用于发送TCP数据。此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为非负值表示写入成功，且该值为写入的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。
 * @param sock 使用的Socket
 * @param buf 要写入的数据
 * @param len 要写入的数据的长度
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiWrite(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct Popkcel_MultiOperation* mo);
/**发起伪同步写入，通常用于发送TCP数据。此函数会挂起协程。
 * @param sock 连接使用的Socket
 * @param buf 要写入的数据
 * @param len 要写入的数据的长度
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为是非负值表示写入成功，且该值为写入的字节数，若为POPKCEL_ERROR表示显式地失败，为POPKCEL_WOULDBLOCK表示超时。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_write(struct Popkcel_PSSocket* sock, const char* buf, size_t len, int timeout);
/**发起多操作下的伪同步发送到指定地址操作，通常用于发送UDP数据。此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为非负值表示写入成功，且该值为写入的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。
 * @param sock 使用的Socket
 * @param buf 要写入的数据
 * @param len 要写入的数据的长度
 * @param addr 要发送到的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiSendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, struct Popkcel_MultiOperation* mo);
/**发起伪同步发送到指定地址操作，通常用于发送UDP数据。此函数会挂起协程。
 * @param sock 使用的Socket
 * @param buf 要写入的数据
 * @param len 要写入的数据的长度
 * @param addr 要发送到的地址，如果是IPV4，则是sockaddr_in类型。如果是IPV6，则是sockaddr_in6类型
 * @param addrLen addr结构体所占的字节数
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为是非负值表示写入成功，且该值为写入的字节数，若为POPKCEL_ERROR表示显式地失败，为POPKCEL_WOULDBLOCK表示超时。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_sendto(struct Popkcel_PSSocket* sock, const char* buf, size_t len, struct sockaddr* addr, socklen_t addrLen, int timeout);
/**发起多操作下的伪同步读取操作，通常用于接收TCP数据。此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为正数表示读取成功，且该值为读取的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。若为0表示读取结束，通常表明连接已断开。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiRead(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo);
/**发起伪同步读取操作，通常用于接收TCP数据。此函数会挂起协程。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为正数表示读取成功，且该值为读取的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。若为0表示读取结束，通常表明连接已断开。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_read(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout);
/**与read只要有数据就返回不同，readFor会保证只有读取了指定字节数才会返回。popkcel_multiReadFor即是多操作下发起伪同步读取指定字节数操作，这通常用于读取TCP数据。此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为正数表示读取成功，且该值为读取的字节数，这个值可以小于len，这表明在超时或出现错误前，没有读够len字节的数据。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiReadFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct Popkcel_MultiOperation* mo);
/**与read只要有数据就返回不同，readFor会保证只有读取了指定字节数才会返回。popkcel_readFor即是发起伪同步读取指定字节数操作，这通常用于读取TCP数据。此函数会挂起协程。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为正数表示读取成功，且该值为读取的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。若为0表示读取结束，通常表明连接已断开。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_readFor(struct Popkcel_PSSocket* sock, char* buf, size_t len, int timeout);
/**发起多操作下的读取数据及其来源地址操作，这通常用于读取UDP数据。此函数会立即返回。
 * 
 * 在操作结束后，可以调用popkcel_multiOperationGetResult获得操作结果。结果为正数表示读取成功，且该值为读取的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。若为0表示读取结束，通常表明连接已断开。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param addr [out]存放数据来源地址信息的结构体。如果是IPV4，建议用sockaddr_in类型。如果是IPV6，建议用sockaddr_in6类型。注意addr一定要分配在heap上，不能分配在stack上。
 * @param addrLen [out]addr结构体所占的字节数。注意addrLen一定要分配在heap上，不能分配在stack上。
 * @param mo 关联的MultiOperation
 */
LIBPOPKCEL_EXTERN void popkcel_multiRecvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, struct Popkcel_MultiOperation* mo);
/**发起读取数据及其来源地址操作，这通常用于读取UDP数据。此函数会挂起协程。
 * @param sock 使用的Socket
 * @param buf [out]用于接收数据的缓冲区。注意buf一定要分配在heap上，不能分配在stack上。
 * @param len 缓冲区的大小
 * @param addr [out]存放数据来源地址信息的结构体。如果是IPV4，建议用sockaddr_in类型。如果是IPV6，建议用sockaddr_in6类型。注意addr一定要分配在heap上，不能分配在stack上。
 * @param addrLen [out]addr结构体所占的字节数。注意addrLen一定要分配在heap上，不能分配在stack上。
 * @param timeout 超时时长，单位为毫秒。小于等于0表示无限等待。
 * @return 结果为正数表示读取成功，且该值为读取的字节数。为POPKCEL_ERROR表示显式地失败。为POPKCEL_WOULDBLOCK表示超时。若为0表示读取结束，通常表明连接已断开。
 */
LIBPOPKCEL_EXTERN ssize_t popkcel_recvfrom(struct Popkcel_PSSocket* sock, char* buf, size_t len, struct sockaddr* addr, socklen_t* addrLen, int timeout);

///有新连接出现时会执行的回调函数的类型，data是用户指定的数据，fd是新连接的文件描述符，addr是新连接的来源地址，addrLen是addr所占的字节数
typedef void (*Popkcel_FuncAccept)(void* data, Popkcel_HandleType fd, struct sockaddr* addr, socklen_t addrLen);

///用于监听TCP端口
struct Popkcel_Listener
{
    POPKCEL_HANDLEFIELD
    POPKCEL_SOCKETCOMMONFIELD
    ///当新连接出现时会执行的回调函数
    Popkcel_FuncAccept funcAccept;
    ///传入回调函数的用户数据
    void* funcAcceptData;
#ifdef _WIN32
    char buffer[sizeof(struct sockaddr_in6) * 2 + 32];
    Popkcel_HandleType curSock;
#endif
};

/**
 * 初始化Listener
 * @param listener 要初始化的Listener
 * @param loop 将Listener初始化到这个Loop上
 * @param ipv6 是否为ipv6。为0表示ipv4，为1表示仅ipv6。
 * @param fd 如果非0，则表示复用文件描述符为fd的socket
 */
LIBPOPKCEL_EXTERN void popkcel_initListener(struct Popkcel_Listener* listener, struct Popkcel_Loop* loop, char ipv6, Popkcel_HandleType fd);
/**
 * 销毁Listener。这不会将Listener从内存中删除。
 * @param listener 要销毁的Listener
 */
LIBPOPKCEL_EXTERN void popkcel_destroyListener(struct Popkcel_Listener* listener);
/**
 * 监听指定端口
 * @param listener 关联的Listener
 * @param port 要监听的端口
 * @param backlog 新连接队列的最大长度，通常用SOMAXCONN
 * @return 成功返回POPKCEL_OK，失败返回POPKCEL_ERROR
 */
LIBPOPKCEL_EXTERN int popkcel_listen(struct Popkcel_Listener* listener, uint16_t port, int backlog);

///保存一次性回调相关数据的类型
struct Popkcel_OneShot
{
    ///一次性回调是使用Notifier来完成的
    struct Popkcel_Notifier notifier;
    ///回调函数
    Popkcel_FuncCallback cb;
    ///传入回调函数的用户数据
    void* data;
};

///保存event loop相关数据的类型
struct Popkcel_Loop
{
    ///在事件循环函数中执行setjmp所需的jmp_buf
    POPKCJMPBUF jmpBuf;
    ///系统Timer，用于在需要时唤醒Loop
    struct Popkcel_SysTimer sysTimer;
#ifndef _WIN32
#    ifdef __linux__
    struct epoll_event* events;
#    else
    struct kevent* events;
#    endif
#else
    ///当前的OVERLAPPED结构体
    struct Popkcel_IocpCallback* curOverlapped;
    ///当前执行GetQueuedCompletionStatus时所接收的completionKey
    ULONG_PTR completionKey;
    ///当前执行GetQueuedCompletionStatus时所接收的numOfBytes
    DWORD numOfBytes;
#endif
    ///最大事件数，分配的events数组大小，在windows下无意义
    size_t maxEvents;
    ///储存timers的红黑树
    struct Popkcel_Rbtnode* timers;
    ///记录事件循环函数中的局部变量在stack中的位置
    char* stackPos;
    ///当前的Context，用于在协程resume后，可以用threadLoop->curContext来获得当前的Context，以进行stack的恢复
    struct Popkcel_Context* curContext;
    //struct Popkcel_HashInfo** moHash;
    //size_t hashSize;
    ///loop的文件描述符，在Linux下是epoll，在BSD下是kqueue，在Windows下是IOCP
    Popkcel_HandleType loopFd;
    ///事件循环函数中使用，记录总共需要处理的events数量。因为事件循环函数的局部变量容易在stack恢复时被修改，所以用局部变量记录这个不安全。
    int numOfEvents;
    ///事件循环函数中使用，记录当前正在处理的event的索引。因为事件循环函数的局部变量容易在stack恢复时被修改，所以用局部变量记录这个不安全。
    int curIndex;
    ///事件循环函数中使用，记录是否已初始化。因为事件循环函数的局部变量容易在stack恢复时被修改，所以用局部变量记录这个不安全。
    char inited;
    ///Loop是否在运行中
    char running;
};

/**
 * 初始化Loop
 * @param loop 要初使化的Loop
 * @param maxEvents 最大事件数，分配的events数组大小，为0表示取默认值。在windows下无意义
 */
LIBPOPKCEL_EXTERN void popkcel_initLoop(struct Popkcel_Loop* loop, size_t maxEvents);
/**
 * 销毁Loop。这不会将Loop从内存中删除。
 * @param loop 要销毁的Loop
 */
LIBPOPKCEL_EXTERN void popkcel_destroyLoop(struct Popkcel_Loop* loop);
/**
 * 内部使用，检查Loop中的Timer是否已达到触发条件。
 * @return 下一个未触发的Timer将在多久后触发，单位为毫秒。如果为-1,那么表示当前没有Timer
 */
int popkcel__checkTimers();
/**
 * 获得当前的时间戳，单位为毫秒。
 * @return 返回当前的时间戳，单位为毫秒。
 */
LIBPOPKCEL_EXTERN int64_t popkcel_getCurrentTime();
/**
 * 将Handle加入到Loop中，Handle是许多类型如Socket、Notifier等的“基类”
 * @param loop 要加入到的Loop
 * @param handle 要加入的Handle
 * @param ev 监听哪些事件，仅在UNIX下有意义。为0表示监听IN、OUT、ERROR，并设为边缘触发。详见Popkcel_Event enum
 * @return 为POPKCEL_OK表示加入成功，否则表示加入失败。
 */
LIBPOPKCEL_EXTERN int popkcel_addHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle, int ev);
/**
 * 从Loop中移除Handle。在windows下，由于IOCP的限制，此函数无意义。
 * @param loop 从哪个Loop移除
 * @param handle 要移除的Handle
 * @return 为POPKCEL_OK表示移除成功，否则表示移除失败。
 */
LIBPOPKCEL_EXTERN int popkcel_removeHandle(struct Popkcel_Loop* loop, struct Popkcel_Handle* handle);
/**
 * 设定一次性触发函数，此函数将在下一轮事件循环中被执行。
 * @param loop 相关的Loop
 * @param cb 要执行的函数
 * @param data 传入函数的用户数据
 */
LIBPOPKCEL_EXTERN void popkcel_oneShotCallback(struct Popkcel_Loop* loop, Popkcel_FuncCallback cb, void* data);

#ifdef POPKCEL_SINGLETHREAD
#    define Popkcel_ThreadType int
#else
#    define Popkcel_ThreadType pthread_t
#endif
///Loop池，用于在多线程中运行不同的Loop。Windows下目前只支持单线程
struct Popkcel_LoopPool
{
    ///Loop数组
    struct Popkcel_Loop* loops;
    ///线程类型数组
    Popkcel_ThreadType* threads;
    ///Loop的数量
    size_t loopSize;
    ///如果执行了popkcel_loopPoolRun，则为1。如果执行了popkcel_loopPoolDetach，则为0。
    char isRun;
};

/**
 * 初始化Loop池
 * @param loopPool 要初始化的Loop池
 * @param loopSize Loop的数量，也即线程数
 * @param maxEvents 传入Loop的参数，表示最大事件数，即分配的events数组大小，为0表示取默认值。在windows下无意义
 */
LIBPOPKCEL_EXTERN void popkcel_initLoopPool(struct Popkcel_LoopPool* loopPool, size_t loopSize, size_t maxEvents);
/**
 * 销毁LoopPool，这不会将LoopPool从内存中删除
 * @param loopPool 要销毁的LoopPool
 */
LIBPOPKCEL_EXTERN void popkcel_destroyLoopPool(struct Popkcel_LoopPool* loopPool);
/**
 * 以Detach方式运行所有的Loop，即当前线程不执行Loop，所以Loop都放在其它线程执行。
 * @param loopPool 相关的LoopPool
 */
LIBPOPKCEL_EXTERN void popkcel_loopPoolDetach(struct Popkcel_LoopPool* loopPool);
/**
 * 把第一个Loop放在本线程中执行，其它Loop放在其它的线程单独执行。此函数会阻塞线程。
 * @param loopPool 相关的LoopPool
 * @return 返回值等于本线程执行的Loop的返回值
 */
LIBPOPKCEL_EXTERN int popkcel_loopPoolRun(struct Popkcel_LoopPool* loopPool);
/**
 * 在不同线程中移动Socket
 * @param loopPool 关联的LoopPool
 * @param threadNum 要移动到的Loop在Loop数组中的索引
 * @param sock 要移动的Socket
 */
LIBPOPKCEL_EXTERN void popkcel_moveSocket(struct Popkcel_LoopPool* loopPool, size_t threadNum, struct Popkcel_Socket* sock);

///当前线程正在运行中的Loop
extern POPKCEL_THREADLOCAL struct Popkcel_Loop* popkcel_threadLoop;

/**
 * 运行Loop，此函数会阻塞线程。
 * @param loop 要运行的Loop
 * @return Loop的执行结果
 */
LIBPOPKCEL_EXTERN int popkcel_runLoop(struct Popkcel_Loop* loop);
/**
 * 初始化popkcel库，在运行popkcel库内的其它函数前，必须先运行一次popkcel_init。
 * @return 如果为POPKCEL_OK，表示初始化成功，否则表示初始化失败。
 */
LIBPOPKCEL_EXTERN int popkcel_init();
/**
 * 将字符串转换为ipv4地址
 * @param addr [out]接收转换地址数据的结构体
 * @param ip 要转换的ipv4地址字符串
 * @param port 此地址关联的端口
 * @return 返回POPKCEL_OK表示转换成功，否则表示转换失败。
 */
LIBPOPKCEL_EXTERN int popkcel_address(struct sockaddr_in* addr, const char* ip, uint16_t port);
/**
 * 将字符串转换为ipv6地址
 * @param addr [out]接收转换地址数据的结构体
 * @param ip 要转换的ipv6地址字符串
 * @param port 此地址关联的端口
 * @return 返回POPKCEL_OK表示转换成功，否则表示转换失败。
 */
LIBPOPKCEL_EXTERN int popkcel_address6(struct sockaddr_in6* addr, const char* ip, uint16_t port);
/**
 * 用数字形式的ipv4地址构造sockaddr_in结构体
 * @param ip 32bit的ipv4地址，如192.168.2.1要表示为192<<24|168<<16|2<<8|1。
 * @param port 此地址关联的端口
 * @return 地址数据结构体
 */
LIBPOPKCEL_EXTERN struct sockaddr_in popkcel_addressI(uint32_t ip, uint16_t port);
/**
 * 恢复指定的协程
 * @param context 要恢复的协程的关联数据
 */
LIBPOPKCEL_EXTERN void popkcel_resume(struct Popkcel_Context* context);
/**
 * 挂起当前协程
 * @param context [out]存储协程相关数据的结构体
 */
LIBPOPKCEL_EXTERN void popkcel_suspend(struct Popkcel_Context* context);

uint32_t popkcel__rand();
void popkcel__globalInit();

struct Popkcel_GlobalVar
{
    struct sockaddr_in tempAddr;
    struct sockaddr_in6 tempAddr6;
    uint32_t seed;
};
extern struct Popkcel_GlobalVar* popkcel_globalVar;

#ifndef _WIN32
int popkcel__notifierInRedo(void* data, intptr_t ev);
void popkcel__eventCall(struct Popkcel_SingleOperation* so, int event);
#endif

#ifdef __cplusplus
}
#endif

#endif
