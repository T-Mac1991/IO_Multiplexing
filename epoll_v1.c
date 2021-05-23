#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>

int epfd = 0;
void closefd(int fd)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("argc error\n");
        return -1;
    }
    
    int port = atoi(argv[1]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        printf("socket error\n");
        return -1;
    }
   
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        printf("bind error\n");
        return -1;
    }

    if (listen(fd, 10)< 0) 
    {
        printf("listen error\n");
        return -1;
    }

    epfd = epoll_create(1);
    if (epfd < 0)
    {
        printf("epoll_create error\n");
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN; // 监听fd最好用默认的EPOLLLT
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        printf("epoll_ctl error\n");
        return -1;
    }

    printf("starting loop...\n");
    struct epoll_event events[512] = {0};
    while (1)
    {
        int n = epoll_wait(epfd, events, 512, -1);
        if (n < 0)
        {
            printf("end of loop\n");
            return -1;
        }

        char buf[1024] = {0};

        for (int i = 0; i < n; i++)
        {
            if (events[i].events == EPOLLIN)
            {
                if (events[i].data.fd == fd)
                {
                    struct sockaddr_in cli_addr;
                    memset(&cli_addr, 0, sizeof(cli_addr));
                    int cli_addr_len = sizeof(cli_addr);
                    int clifd = accept(events[i].data.fd, (struct sockaddr*)&cli_addr, &cli_addr_len); 
                    if (clifd < 0)
                        continue;

                    char tmp[256] = {0};
                    printf("connected with %s:%d\n", inet_ntop(AF_INET, &cli_addr.sin_addr, tmp, 256),
                        ntohs(cli_addr.sin_port));

                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = clifd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, clifd, &ev) < 0)
                        continue;
                }
                else
                {
                    int ret = recv(events[i].data.fd, buf, 1024, 0);
                    if (ret < 0) 
                    {
                        if (errno == EAGAIN || EWOULDBLOCK)
                            continue;
                        printf("recv errno: %d\n", errno);
                        closefd(events[i].data.fd);
                    }
                    else if (ret == 0)
                    {
                        printf("socket closed\n");
                        closefd(events[i].data.fd);
                    }
                    else
                    {
                        printf("recv buffer: %s\n", buf);
                        struct epoll_event ev;
                        ev.events = EPOLLOUT | EPOLLET;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &ev);
                    }
                }
            }

            if (events[i].events == EPOLLOUT)
            {
                int ret = send(events[i].data.fd, buf, strlen(buf), 0);
                if (ret < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    
                    closefd(events[i].data.fd);
                }
                else 
                {
                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &ev);

                }
            }
        }
    }
    printf("end of loop\n");
    return 0;
}