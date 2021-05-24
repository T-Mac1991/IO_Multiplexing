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
char buf[1024] = {0};

struct SockItem
{
    int fd;
    int (*callback)(struct SockItem *si);
};

void closefd(int fd)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
    close(fd);
}

int recv_cb(struct SockItem *si);
int send_cb(struct SockItem *si)
{
    int fd = si->fd;

    strcat(buf, "(send)");
    printf("send: %s\n", buf);
    int ret = send(fd, buf, strlen(buf), 0);
    if (ret < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        
        closefd(fd);
        free(si);
    }
    else 
    {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        si->callback = recv_cb;
        ev.data.ptr = (void*)si;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);

        memset(buf, 0, 1024);
    }
    return 0;
}

int recv_cb(struct SockItem *si)
{
    int fd = si->fd;
    int ret = recv(fd, buf, 1024, 0);
    if (ret < 0) 
    {
        if (errno == EAGAIN || EWOULDBLOCK)
            return -1;
        printf("recv errno: %d\n", errno);
        closefd(fd);
        free(si);
    }
    else if (ret == 0)
    {
        printf("socket closed\n");
        closefd(fd);
        free(si);
    }
    else
    {
        printf("recv buffer: %s\n", buf);
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLET;
        si->callback = send_cb;
        ev.data.ptr = (void*)si;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }
    return 0;
}

int accept_cb(struct SockItem *si)
{
    int fd = si->fd;
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    int cli_addr_len = sizeof(cli_addr);
    int clifd = accept(fd, (struct sockaddr*)&cli_addr, &cli_addr_len); 
    if (clifd < 0)
        return -1;

    char tmp[256] = {0};
    printf("connected with %s:%d\n", inet_ntop(AF_INET, &cli_addr.sin_addr, tmp, 256),
        ntohs(cli_addr.sin_port));

    struct SockItem *si2 = (struct SockItem*)malloc(sizeof(struct SockItem));
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    //ev.data.fd = clifd;
    si2->callback = recv_cb;
    si2->fd = clifd;
    ev.data.ptr = (void*)si2;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, clifd, &ev) < 0)
        return -1;

    return 0;
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
    //ev.data.fd = fd;
    
    struct SockItem *si = (struct SockItem *)malloc(sizeof(struct SockItem));
    si->fd = fd;
    si->callback = accept_cb;
    ev.data.ptr = (void*)si;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        printf("epoll_ctl error\n");
        return -1;
    }

    printf("starting loop...\n");
    struct epoll_event events[512] = {0};
    char buf[1024] = {0};
    while (1)
    {
        int n = epoll_wait(epfd, events, 512, -1);
        if (n < 0)
        {
            printf("end of loop\n");
            return -1;
        }


        for (int i = 0; i < n; i++)
        {
            if (events[i].events == EPOLLIN)
            {
                struct SockItem *si = (struct SockItem *)events[i].data.ptr;
                si->callback(si);
            }

            if (events[i].events == EPOLLOUT)
            {
                struct SockItem *si = (struct SockItem *)events[i].data.ptr;
                si->callback(si);
            }
        }
    }
    printf("end of loop\n");
    return 0;
}