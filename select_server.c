#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <memory.h>

char buf[1024] = {0};

int conn_accept(int fd, fd_set *readfs)
{
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    socklen_t len = sizeof(cli_addr);
    int clifd = accept(fd, (struct sockaddr *)&cli_addr, &len);
    if (clifd < 0)
    {
        printf("accept error: %d\n", errno);
        return -1; 
    }

    FD_SET(clifd, readfs);
    
    char str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &cli_addr.sin_addr, str, sizeof(str));
	printf("recv from %s at port %d\n", str, ntohs(cli_addr.sin_port));
    return 0;
}

int conn_read(int fd, fd_set *readfs, fd_set *writefs)
{
    // 收到数据
    int nread;
    ioctl(fd, FIONREAD, &nread);//取得数据量交给nread
    if (nread == 0)
    {
        printf("recv len is 0, connection closed, errno: %d\n", errno);
        FD_CLR(fd, readfs);
        return 0; 
    }
    else
    {
        int nread = recv(fd, buf, sizeof(buf), 0);
        if (nread == 0)
        {
            printf("connection closed: %d\n", errno);
            FD_CLR(fd, readfs);
            close(fd);
            return 0; 
        }
        else if (nread == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;

            printf("recv error: %d\n", errno);
            FD_CLR(fd, readfs);
            close(fd);
            return -1;
        }
        else
        {
            printf("recv buf: %s\n", buf);

            FD_CLR(fd, readfs);
            FD_SET(fd, writefs);
        }
    }
    return 0;
}

int conn_write(int fd, fd_set *readfs, fd_set *writefs)
{
    int ret = send(fd, buf, strlen(buf), 0);
    if (ret < 0)
    {
        printf("send error: %d\n", errno);
        FD_CLR(fd, writefs);
        close(fd);
        return -1;
    }

    FD_SET(fd, readfs);
    FD_CLR(fd, writefs);
    return 0;
}

int check_endian(){
    union {
        int i;
        char c;
    }x;
    x.i = 1;
    if (x.c == 1)
        return 1; // Little Endian(低尾端：尾部在低地址)
    return 0; // Big Endian(高尾端：尾部在高地址)
}

int main(int argc, char **argv)
{
    printf("%s\n", check_endian() == 0 ? "Big Endian" : "Little Endian");
    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0)
    {
        printf("socket error: %d\n", errno);
        return -1;
    }

    int port = 8888;
    if (argc == 2)
        port = atoi(argv[1]);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(serverfd, (struct sockaddr *)&addr, sizeof(addr)))
    {
        printf("bind error: %d\n", errno);
        return -1;
    }

    if (listen(serverfd, 10) < 0)
    {
        printf("listen error: %d\n", errno);
        return -1;
    }

    fd_set readfs;
    fd_set writefs;
    fd_set exceptfs;
    fd_set tmpreadfs;
    fd_set tmpwritefs;
    FD_ZERO(&readfs);
    FD_ZERO(&writefs);
    FD_ZERO(&exceptfs);

    FD_SET(serverfd, &readfs); //监听fd

    while (1)
    {
        tmpreadfs = readfs;//将需要监视的描述符集copy到select查询队列中，select会对其修改，所以一定要分开使用变量
        tmpwritefs = writefs;
        int ret = select(FD_SETSIZE, &tmpreadfs, &tmpwritefs, &exceptfs, 0);
        if (ret < 0) 
        {
            printf("select error: %d\n", errno);
            return -1;           
        }
        else if (ret == 0) 
        {
            printf("select timeout\n");
        }
        else
        {
            for (int fd = 0; fd < FD_SETSIZE; fd++) 
            {
                if(FD_ISSET(fd, &tmpreadfs))
                {
                    if (fd == serverfd) // accept
                    {
                        conn_accept(fd, &readfs);
                    }
                    else
                    {
                        conn_read(fd, &readfs, &writefs);
                    }
                }
                else if (FD_ISSET(fd, &writefs))
                {
                    conn_write(fd, &readfs, &writefs);
                }
                else if (FD_ISSET(fd, &exceptfs))
                {
                    FD_CLR(fd, &exceptfs);
                    close(fd);
                    return 0;
                }
            }
        }
    }

}