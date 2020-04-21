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
#include <functional>
#include <iostream>
#include <memory>
#include <string.h>
#include <string>

#ifndef _WIN32
#    include <arpa/inet.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

#include <popkcel.h>

using namespace std;
using namespace popkcel;
MultiOperation* curMo;

struct DoOnDelete
{
    function<void()> f;
    DoOnDelete(const function<void()>& f)
        : f(f)
    {
    }
    ~DoOnDelete()
    {
        f();
    }
};

void kkk()
{
    string sn;
    DoOnDelete dod([]() {
        cout << "Do not call me!" << endl;
    });
    PSSocket* so = new PSSocket(threadLoop, SOCKETTYPE_TCP);
    PSSocket* so2 = new PSSocket(threadLoop, SOCKETTYPE_TCP);
    auto addr = address("127.0.0.1", 1234);
    auto addr2 = address("8.5.2.3", 1235);
    std::unique_ptr<MultiOperation> mo(new MultiOperation(threadLoop));
    curMo = mo.get();
    so->multiConnect((sockaddr*)&addr, sizeof(addr), mo.get());
    so2->multiConnect((sockaddr*)&addr2, sizeof(addr), mo.get());
    mo->wait(999, 1);
    if (mo->curSocket == so) {
        sn = "so ";
        so->write("abc", 3);
    }
    else if (mo->curSocket == so2) {
        sn = "so2 ";
        so2->write("def", 3);
    }
    if (mo->curSocket && mo->mapRv[mo->curSocket] == SOCKRV_OK)
        sn += "ok.";
    else
        sn += "failed.";
    cout << sn << endl;
    mo->reBlock();
    if (mo->timeOuted) {
        cout << "Timeout." << endl;
    }
    char* buf = new char[10];
    int r;
    while ((r = so->read(buf, 10)) > 0) {
        cout.write(buf, r);
        cout.flush();
    }
    //auto s = GetLastErrorAsString();
}

void la(HandleType fd, sockaddr* addr, socklen_t len)
{
    cout << "new connection." << endl;
    PSSocket* ps = new PSSocket(threadLoop, fd);
    ps->write("xxx", 3);
    char* buf = new char[10];
    int r;
    while ((r = ps->read(buf, 10)) > 0) {
        cout.write(buf, r);
        cout.flush();
    }
    delete ps;
}

int main(int argc, char* argv[])
{
    int r = popkcel::init();
    //auto s = GetLastErrorAsString();
    Loop loop;
    //loop.setupFirstTimeCb(kkk);
    Listener ls(&loop);
    r = ls.listen(1111);
    if (r == SOCKRV_ERROR) {
        cout << "listen failed." << endl;
        return 1;
    }
    ls.funcAccept = &la;
    runLoop(&loop);
    return 0;
}
