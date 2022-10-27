# popkcel
一个用纯C实现的跨平台的简单的异步网络库，带有协程功能。

popkcel是“popkc的event loop”的意思，它支持epoll、kqueue和IOCP。相比于libuv，它的特点是轻量、简单，而且支持一种类似于协程的功能，使得你可以像写同步代码一样地写异步代码。它还支持一种简单的可靠UDP传输协议PSR（既Popkc's Simple Reliable UDP），可以利用UDP打洞的原理为内网机器之间提供类似TCP的可靠连接。

popkcel用简单的方法来实现协程。它先在loop的事件循环函数中用setjmp来标记一个点，并记录这个点在stack中的位置。当某个协程需要挂起时，这个协程也标记自己的当前点，并把当前点到事件循环函数中的点之间的stack保存下来，然后longjmp到那个点上。要恢复协程，就longjmp到协程之前保存的点上，再用memcpy把之前保存的stack恢复。

## 编译方法

使用CMAKE进行编译即可。popkcel在UNIX下仅依赖pthread，基本可以算是没有第三方依赖的。编译非常简单，使用常规的CMAKE编译方法即可。

如：
```shell
cmake .
make
```
## 使用方法

先包含头文件。
```c
#include <popkcel.h>
```
使用popkcel时，需要先执行popkcel_init()进行初始化。
```c
popkcel_init();
```
然后需要构造一个Popkcel_Loop类型。
```c
struct Popkcel_Loop loop;
popkcel_initLoop(&loop, 0);
```
如果你需要监听端口，那么像这样做：
```c
struct Popkcel_Listener listener;
popkcel_initListener(&listener, &loop, 0, 0);
listener.funcAccept = &acceptCb;//设置回调函数
listener.funcAcceptData = &listener;//设置传送给回调函数的void*数据
popkcel_listen(&listener, 1234, SOMAXCONN);//监听1234端口
```
popkcel支持像libuv那样的简单的异步网络操作，如：
```c
struct Popkcel_Socket* sock = (struct Popkcel_Socket*)malloc(sizeof(struct Popkcel_Socket));
popkcel_initSocket(sock, popkcel_threadLoop, POPKCEL_SOCKETTYPE_UDP, 0);
popkcel_trySendto(sock, buffer, bufferLen, addr, addrLen, cb, userData);
```
这些标准的异步操作都需要有一个回调函数，使用起来比较麻烦。popkcel还提供了一种伪同步的socket操作，它的语法类似同步操作，但它实际上是使用类似协程方法实现的异步操作，因此它的性能也会低于异步操作。使用伪同步操作的好处是不需要像同步操作那样为每个SOCKET提供额外的线程，同时能避免像异步操作那样子使用麻烦的、大量的回调函数。

要使用popkcel的伪同步socket，需要loop在运行状态下才行，而loop在运行状态时线程就被卡住了无法执行后续代码，所以我们需要用一个一次性回调函数，来让loop在运行时执行我们的函数。
```c
popkcel_oneShotCallback(&loop, oneShotCb, NULL);
//回调函数为oneShotCb，传递给该函数的void*数据为NULL
```
oneShotCb的内容如下
```c
void oneShotCb(void* data, intptr_t rv)
{
        struct Popkcel_PSSocket *ps = malloc(sizeof(struct Popkcel_PSSocket));
        //注意！Popkcel_PSSocket一定要分配在heap上，一定不能分配在stack上。你要明白我们实际上还是在用异步操作，也就是执行Popkcel_PSSocket相关的函数时，我们相当于从当前的函数中返回了。如果Popkcel_PSSocket分配在了stack上，那么它在内存中的数据实际上已经不可靠了，于是程序就会崩溃。
        popkcel_initPSSocket(ps, popkcel_threadLoop, POPKCEL_SOCKETTYPE_TCP, 0);
        //创建一个TCP socket，popkcel_threadLoop是当前线程中正在运行的Popkcel_Loop。
        struct sockaddr_in addr;
        popkcel_address(&addr, "127.0.0.1", 1234);
        //将addr的地址设为127.0.0.1:1234
        int r = popkcel_connect(ps, (struct sockaddr*)&addr, sizeof(struct sockaddr_in), 3000);
        //连接指定地址，超过3000毫秒无回应的话也会返回。
        if (r >= 0) {
                popkcel_write(ps, "你好", 6, 0);
                myBuf = (char*)malloc(5);//myBuf是个全局的char*变量
                while ((r = popkcel_read(ps, myBuf, 5, 0)) > 0) {
                        //将读到数据显示在屏幕上
                        fwrite(myBuf, 1, r, stdout);
                        fflush(stdout);
                }
        }
}
```
以上是操作单个socket的例子，也是最常用的情况，不过我们有时候也需要在同一协程中同时操作多个socket，比如你需要同时发送8个连接请求，那么等待他们一个返回后再执行下一个显然很傻，popkcel支持同时等待多个socket的伪同步操作，只要使用popkcel_multi*函数即可。
```c
struct Popkcel_MultiOperation* mo = malloc(sizeof(struct Popkcel_MultiOperation));
//和PSSocket一样，不要在stack上创建Popkcel_MultiOperation
popkcel_initMultiOperation(mo, popkcel_threadLoop);
//初始化一个Popkcel_MultiOperation类型
popkcel_multiConnect(sock1, (struct sockaddr*)&addr, sizeof(struct sockaddr_in), mo);
popkcel_multiConnect(sock2, (struct sockaddr*)&addr2, sizeof(struct sockaddr_in), mo);
...
popkcel_multiConnect(sock8, (struct sockaddr*)&addr8, sizeof(struct sockaddr_in), mo);
//连接指定地址
popkcel_multiOperationWait(mo, 15000, 0);
//协程将在此处挂起，当所有连接出结果（成功或失败），或者15秒过后，协程将继续执行后续代码
if (mo->timeOuted)//判断是否超时
...
intptr_t r = popkcel_multiOperationGetResult(mo, sock1);
//r即是sock1 connect的结果，值为POPKCEL_OK则表示连接成功，为POPKCEL_ERROR表示连接显式地返回错误，为POPKCEL_WOULDBLOCK表示连接超时
```
如果你想要在其中一个socket连接出结果时立即处理，那么只需把popkcel_multiOperationWait函数的第三个参数改为非0即可。
```c
popkcel_multiOperationWait(mo, 15000, 1);
if (mo->curSocket == sock1)//判断当前出结果的socket是否为sock1
...
popkcel_multiOperationReblock(mo);
//使协程重新在popkcel_multiOperationWait处挂起。若所有操作都有结果，或者超时了，那么popkcel_multiOperationReblock会立即返回，并执行后面的代码。
```
popkcel支持ipv6，初始化socket时加上POPKCEL_SOCKETTYPE_IPV6即可。
```c
popkcel_initSocket(sock, (Popkcel_Loop*)data, POPKCEL_SOCKETTYPE_TCP | POPKCEL_SOCKETTYPE_IPV6, 0);
```
popkcel_initListener函数的第三个参数为1时表示监听ipv6地址。

popkcel还支持通过udp方式模拟tcp连接。popkcel支持一种名为psr的协议，这协议是我自己定义的，它比较简单，可以让udp连接像tcp一样可靠。它当然不是真的tcp连接，它只是让udp像tcp一样可靠。要知道在ipv6还没有彻底流行的现在，要在两台不同内网的计算机间进行点对点的可靠通信是非常麻烦的事，而这就是psr协议要解决的事情。类似的协议已经有不少了（如utorrent的utp），但我的psr胜在简单，如果只是进行简单的通信而不是进行大文件传输的话，用psr就够了。

可以参考test里的代码来使用psr。psr目前只支持异步操作，暂不支持伪同步操作。

## 操作系统支持

popkcel在这些操作系统中测试过：Linux，Windows，MacOS，FreeBSD

理论上它也该支持Android和iOS，不过我还没有测试过。

## WINDOWS下的限制

popkcel主要还是为linux写的，目前在windows下只支持单线程。

## FreeBSD下的限制

只能静态链接popkcel，不能动态链接popkcel。FreeBSD的thread local storage对动态库的支持似乎有问题，我搞不定。说实话在FreeBSD上测试只是方便我在虚拟机中调试kqueue相关代码，为了kqueue=为了MacOS和iOS，所以也懒得搞了。
