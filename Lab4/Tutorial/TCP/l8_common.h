#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
    (__extension__({                               \
        long int __result;                         \
        do                                         \
            __result = (long int)(expression);     \
        while (__result == -1L && errno == EINTR); \
        __result;                                  \
    }))
#endif

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int make_local_socket(char *name, struct sockaddr_un *addr)
{
    int socketfd;
    if(socketfd = socket(AF_UNIX, SOCK_STREAM,0)<0) ERR("socketfd"); //create a socket
    strncpy(addr->sun_path, name, sizeof(addr->sun_path) - 1); //copy path to the sockaddr_un
    return socketfd;
}

int connect_local_socket(char* name) // connect to this 'name'
{
    struct sockaddr_un addr;
    int socketfd = make_local_socket(name,&addr);
    if(connect(socketfd,(struct sockaddr*)&addr, SUN_LEN(&addr))<0) ERR("connect"); //i want to connect to this one
    return socketfd; 
}

int bind_local_socket(char *name, int backlog_size)
{
    if (unlink(name) < 0 && errno != ENOENT)
        ERR("unlink");

    struct sockaddr_un addr;
    int socketfd = make_local_socket(name,&addr);   
    if(bind(socketfd,(struct sockaddr*)&addr,SUN_LEN(&addr))<0) ERR("bind"); //this is my endpoint

    if(listen(socketfd,backlog_size)<0) ERR("listen"); //start watiing for clients
}

int make_tcp_socket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM,0);
    if(sockfd<0) ERR("socket");
    return sockfd;
}

struct sockaddr_in make_address(char *address, char *port) //take human readable addr(127,0,0) & port "80" and turn that into addr
{
    int ret, sockfd = make_tcp_socket();
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if(ret = getaddrinfo(address,port,&hints,&result)) ERR("getaddrinfo");

    addr = *(struct sockaddr_in*)(result->ai_addr);
    free(result);
    return addr;
}

int connect_tcp_socket(char *name, char *port)
{
    struct sockaddr_in addr;
    int socketfd;
    socketfd = make_tcp_socket();
    addr = make_address(name, port);
    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        ERR("connect");
    }
    return socketfd;
}

int bind_tcp_socket(uint16_t port, int backlog_size)
{
    int sockfd, t=1;    
    struct sockaddr_in addr;
    sockfd = make_tcp_socket();
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(sockfd, backlog_size) < 0)
        ERR("listen");
    return sockfd;
}

int add_new_client(int sfd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}