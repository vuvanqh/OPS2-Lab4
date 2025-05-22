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
#include <pthread.h>

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

typedef struct data_t
{
    int id;
    int choice;
}data_t;

typedef struct 
{
    int port;
    pthread_mutex_t* mutex;
    int votes[3];
}udp_t;

int bind_tcp(int port)
{
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd<0) ERR("socket"); 
    struct sockaddr_in addr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0)
        ERR("bind");

    if(listen(sockfd,10)<0) ERR("listen");
    return sockfd;
}

int accept_tcp(int sock)
{
    int sockfd;
    if((sockfd = accept(sock,0,0))<0) 
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return sockfd;
}

void make_nonblocking(int fd)
{
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);
}
void work(int sock,int votes[3])
{
    make_nonblocking(sock);
    data_t data[7];
    int connected = 0;
    for(int i=0;i<7;i++)
    {
        data[i].choice = -1;
        data[i].id = -1;
    }

    int epoll = epoll_create1(0);
    struct epoll_event event,events[10];
    event.data.fd = sock;
    event.events = EPOLLIN;
    if(epoll_ctl(epoll,EPOLL_CTL_ADD,sock,&event)<0)
        ERR("epoll_ctl");

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    while(do_work)
    {
        int nds = epoll_pwait(epoll,events,10,-1,&oldmask);
        for(int i=0;i<nds;i++)
        {
            if(events[i].data.fd==sock)
            {
                printf("Client connected\n");
                int client = accept_tcp(events[i].data.fd);
                make_nonblocking(client);

                event.data.fd = client;
                event.events = EPOLLIN | EPOLLRDHUP;
                if(epoll_ctl(epoll,EPOLL_CTL_ADD,client,&event)<0)
                    ERR("epoll_ctl - client");
            }
            else
            {
                char x;
                int res = recv(events[i].data.fd,&x,1,0);
                int msg = atoi(&x);
                if(res == 0 || events[i].events&EPOLLRDHUP)
                {
                    for(int k=0;k<7;k++)
                    {
                        if(data[k].id == events[i].data.fd)
                        {
                            data[k].id=-1;
                            data[k].choice=-1;
                        }
                    }  
                    if(epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,0)<0)
                        ERR("epoll_ctl remove");

                    printf("client[%d] disconnected\n",events[i].data.fd);
                    close(events[i].data.fd);
                }   
                else
                {
                    int flag=0;
                    int id;
                    for(int k=0;k<7;k++)
                    {
                        if(data[k].id==events[i].data.fd)
                        {
                            id = k;
                            flag=1;
                            break;
                        }
                    }
                    if(flag)
                    {
                        if(msg<1 || msg>3) 
                        {
                            printf("invalid vote\n");
                            break;
                        }
                        votes[msg-1]++;
                        if(data[id].choice!=-1)
                        {
                            votes[data[id].choice-1] --;
                        }
                        data[id].choice = msg-1;
                    }
                    else
                    {
                        if(connected==7 || data[msg-1].id!=-1)
                        {
                            if(epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,0)<0)
                                 ERR("epoll_ctl remove");

                            printf("client[%d] disconnected\n",events[i].data.fd);
                            close(events[i].data.fd);
                        }
                        else
                        {
                            data[msg-1].id=events[i].data.fd;
                            connected++;

                            char welcome[255];
                            snprintf(welcome,sizeof(welcome),"Welcome, elector of %d\n",msg);
                            send(events[i].data.fd, welcome, strlen(welcome), 0);
                        }
                    }
                }
            }
        }
    }
    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);
    close(epoll);
}

void udp_send(void*args)
{
    udp_t* data = (udp_t*)args;
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(data->port);
    int sock = socket(AF_INET,SOCK_DGRAM,0);
    char msg[128];

    while(do_work)
    {
        sleep(1);
        pthread_mutex_lock(data->mutex);
        snprintf(msg, sizeof(msg), "Results:\n\t A=%d, \n\t B=%d, \n\t C=%d\n", data->votes[0], data->votes[1], data->votes[2]);
        pthread_mutex_unlock(data->mutex);
        sendto(sock,msg,strlen(msg), 0, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
    }
    close(sock);
}

int main(int argc,char** argv)
{
    if(argc!=3) usage(argv[0]);

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");

    int votes[3] = {0};
    int sockfd = bind_tcp(atoi(argv[1]));
    pthread_t tid;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    udp_t udp = {.mutex = &mutex, .port = atoi(argv[2]), .votes = votes};

    if(pthread_create(&tid,NULL,udp_send,(void*)&udp)!=0) 
        ERR("pthread_create");

    work(sockfd,votes);

    close(sockfd);
    pthread_mutex_destroy(&mutex);
    return 0;
}