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

int main(int argc,char**argv)
{
    if(argc!=3) usage(argv[0]);

    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;

    if(getaddrinfo(argv[1],argv[2],&hints,&result)<0) 
        ERR("getaddrinfo");

    addr = *(struct sockaddr_in*)result->ai_addr;
    freeaddrinfo(result);
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if(sock<0) ERR("sock");
    if(connect(sock,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0)
        ERR("connect");
    int32_t data = htonl(getpid());
    send(sock,(char*)&data,sizeof(int32_t),0);
    recv(sock,(char*)&data,sizeof(int32_t),0);
    if(ntohl(data))
        printf("From client: [%d]sum = %d\n",getpid(),ntohl(data));
    
    close(sock);
    return 0;
}