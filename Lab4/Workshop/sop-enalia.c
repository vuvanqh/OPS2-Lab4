#include "common.h"

#define MAX_CLIENTS 4
#define MAX_USERNAME_LENGTH 32
#define MAX_MESSAGE_SIZE 64

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");

    fprintf(stderr, "\t%s", program_name);
    set_color(2, SOP_PINK);
    fprintf(stderr, " port\n");

    fprintf(stderr, "\t  port");
    reset_color(2);
    fprintf(stderr, " - the port on which the server will run\n");

    exit(EXIT_FAILURE);
}

int bind_tcp(int port)
{
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if(bind(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0) 
        ERR("bind");
    if(listen(sockfd,MAX_CLIENTS)<0)
        ERR("listen");
    
    return sockfd;
}

int new_client(int fd)
{
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if((sock = accept(fd,0,0))<0) 
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return sock;
}

void make_nonblock(int fd)
{
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0) | O_NONBLOCK);
}

void do_work(int port)
{
    int epoll = epoll_create1(0);
    struct epoll_event event,events[MAX_CLIENTS+1];
    int server = bind_tcp(port);
    make_nonblock(server);
    event.data.fd = server;
    event.events = EPOLLIN;
    int clients[MAX_CLIENTS] = {0};
    int count =0;
    if(epoll_ctl(epoll,EPOLL_CTL_ADD,server,&event)<0)
        ERR("epoll_ctl");

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    int work=1;
    while(work)
    {  
        int nd = epoll_pwait(epoll,events,MAX_CLIENTS+1,-1,&oldmask);
        for(int i=0;i<nd;i++)
        {
            if(events[i].data.fd==server)
            {
                char msg[MAX_MESSAGE_SIZE];
                msg[MAX_MESSAGE_SIZE-1] = '\0';
                int client = new_client(events[i].data.fd);
                printf("[%d] connected\n",client);
                make_nonblock(client);
                if(count==4)
                {
                    strcpy(msg,"Server is full!\n");
                    send(client,msg,sizeof(msg),0);
                    close(client);
                    continue;
                }
                else
                {
                    count++;
                    for(int k=0;k<MAX_CLIENTS;k++)
                    {
                        if(clients[k]==0)
                        {
                            clients[k]=client;
                            break;
                        }
                    }
                    for(int k=0;k<MAX_CLIENTS;k++)
                    {
                        memset(msg,0,MAX_MESSAGE_SIZE-1);
                        if(clients[k]==client)
                        {
                            strcpy(msg,"Please enter your username\n");
                            send(clients[k],msg,sizeof(msg),0);
                        }
                        else
                        {
                            strcpy(msg,"User logging in...\n");
                            send(clients[k],msg,sizeof(msg),0);
                        }
                    }
                }

                event.data.fd = client;
                event.events = EPOLLIN | EPOLLRDHUP;
                if(epoll_ctl(epoll,EPOLL_CTL_ADD,client,&event)<0)
                    ERR("epoll_ctl client");
            }
            else
            {  
                int res;
                char msg[MAX_MESSAGE_SIZE];
                res = recv(events[i].data.fd,msg,MAX_MESSAGE_SIZE-1,0);
                msg[res] = '\0';

                if(res==0 || events[i].events&EPOLLRDHUP)
                {
                    if(epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,NULL)<0)
                        ERR("epoll_ctl client");
                    
                    for(int k=0;k<MAX_CLIENTS;k++)
                    {
                        if(clients[k]==events[i].data.fd)
                        {
                            clients[k]=0;
                            break;
                        }
                    }

                    printf("\n[%d] disconnected\n",events[i].data.fd);
                    count--;
                    close(events[i].data.fd);
                }
                else if(strcmp("\n",msg)==0)
                {
                    snprintf(msg,sizeof(msg),"%s","Hello World!\n");
                    send(events[i].data.fd,msg,MAX_MESSAGE_SIZE,0);
                }
                else
                {
                    printf("[%d] %s",events[i].data.fd,msg);
                }
            }
        }
    }
    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);
    close(epoll);
    close(server);
}

//always add '\0'
int main(int argc, char** argv) 
{
    if(argc!=2) usage(argv[0]); 

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
        
    int port = atoi(argv[1]);
    do_work(port);

    return 0;
}