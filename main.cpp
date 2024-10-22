
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <cstdio>
#include <string.h>
#include "utils/debug.hpp"

int main() {
    // 0号输入流设为非阻塞, 如果read()没有收到消息就会返回EWOULDBLOCK;
    int attr = 1;
    ioctl(0, FIONBIO, &attr);

    int epfd = epoll_create1(0);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = 0;
    epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &event);

    while (true) {
        struct epoll_event ebuf[10];
        int res = epoll_wait(epfd, ebuf, 10, 2000);
        if (res == -1) debug(), "epoll error", strerror(errno);

        if (res == 0) debug(), "No input over 2 seconds...";

        for (int i = 0; i < res; i++) {
            debug(), "Has Data!";
            int fd = (int) ebuf[i].data.fd;
            char c;
            while (true) {
                int len = (int)read(fd, &c, 1);
                if (len <= 0) { // 阻塞
                    debug(), "len <= 0, blocked";
                    break;
                }
                debug(), c;
            }
            debug(), (int) ebuf[i].events, (int) ebuf[i].data.fd;
        }
    }
    return 0;
}