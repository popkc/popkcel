/*
Copyright (C) 2020 popkc(popkcer at gmail dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifdef _WIN32
#    define POPKCEL_SINGLETHREAD
#endif

#include <errno.h>
#include <iostream>
#include <popkcel.hpp>
using namespace popkcel;
using namespace std;

char* buf;
#ifdef _WIN32
//from https://stackoverflow.com/a/17387176
static std::string GetLastErrorAsString()
{
    //Get the error message, if any.
    DWORD errorMessageID = ::GetLastError();
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
#endif
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

void oneShotCb(void* data, intptr_t rv)
{
    cout << "one shot cb" << endl;
    PSSocket* ps = new PSSocket((Loop*)data);
    sockaddr_in addr;
    address(&addr, "127.0.0.1", 1234);
    int r = ps->connect((sockaddr*)&addr, sizeof(addr), 3000);
    cout << "connect result " << r << endl;
    ps->write("abc", 3);
}

ssize_t aaa(ssize_t a)
{
    return a;
}

int main()
{
    popkcel_init();
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
    return 0;
}
