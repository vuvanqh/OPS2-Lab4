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

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) { fprintf(stderr, "USAGE: %s socket port\n", name); }

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int bind_socket(int32_t port)
{
    int sock = socket(AF_INET, SOCK_STREAM,0),t=1;
    if(sock<0) ERR("socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if(bind(sock,(struct sockaddr*)&addr,sizeof(addr))<0) 
        ERR("bind");
    if(listen(sock,5)<0) 
        ERR("listen");
    return sock;
}

int add_new_client(int fd)
{
    int sock = TEMP_FAILURE_RETRY(accept(fd,0,0));
    if(sock<0) 
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return sock;
}

int32_t sum(int num)
{
    int32_t res=0;
    while(num>0)
    {
        res+=num%10;
        num/=10;
    }
    return res;
}

void work(int32_t port)
{
    int maxi=0;
    int epfd = epoll_create1(0);
    if(epfd<0) ERR("epoll_create");
    struct epoll_event event, events[1];
    int sock = bind_socket(port);
    event.data.fd = sock;
    event.events = EPOLLIN;

    if(epoll_ctl(epfd,EPOLL_CTL_ADD,sock,&event)<0)
        ERR("epoll_ctl");
    
    int num;
    pid_t data;
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while(do_work)
    {
        if((num = epoll_pwait(epfd,events,1,-1,&oldmask))<0)
        {
            if(errno!=EINTR)
                ERR("epoll_wait");
        }
        int client = add_new_client(events[0].data.fd);
        int res;
        if((res = recv(client,(void*)&data,sizeof(pid_t),0))<0) ERR("recv");

        if(res == sizeof(pid_t))
        {
            int32_t msg = htonl(sum(ntohl(data)));
            if(send(client,(void*)&msg,sizeof(int32_t),0)<0)
            ERR("send");
            if(msg>maxi) maxi = msg;
        }
        TEMP_FAILURE_RETRY(close(client));
    }
    TEMP_FAILURE_RETRY(close(sock));
    TEMP_FAILURE_RETRY(close(epfd));
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    printf("Highest sum: %d\n",maxi);
}

int main(int argc, char**argv)
{
    if(argc!=2) ERR(argv[0]);
    int32_t port = atoi(argv[1]);
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    work(port);
    return 0;
}
