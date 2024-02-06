#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <iostream>
#include <pthread.h>
#include <poll.h>

using namespace std;

pthread_mutex_t mymutex;

typedef struct fdInfo
{
    int fd;
    struct sockaddr_in cliaddr;
    int *maxfd;
    struct pollfd *fds;
} fdInfo;

typedef struct CommunicateInfo
{
    int fd;
    int *maxfd;
    struct pollfd *fds;
} CommunicateInfo;

void *ConnAccept(void *arg)
{
    fdInfo *info = (fdInfo *)arg;

    int cliaddrLen = sizeof(info->cliaddr);
    int cfd = accept(info->fds[0].fd, (struct sockaddr *)&info->cliaddr, (socklen_t *)&cliaddrLen);
    cout << "連接成功！ " << endl;
    
    // 得到了有效的客户端文件描述符，将这个文件描述符放入读集合当中，并更新最大值
    pthread_mutex_lock(&mymutex);
     for(int i=0; i<1024; ++i)
    {
        if(info->fds[i].fd == -1)
        {
            info->fds[i].fd = cfd;
            break;
        }
    }
    *info->maxfd = cfd > *info->maxfd ? cfd : *info->maxfd;
    pthread_mutex_unlock(&mymutex);
    cout << "设置成功！ " << endl;
    for(int i=0;i<100;i++)
    {
        cout<<" "<<info->fds[i].fd;
        if(i%10==0)
        {
            cout<<endl;
        }
    }
    
    free(info);

    return NULL;
}

void *Communication(void *arg)
{

    cout << "開始通信" << endl;
    CommunicateInfo *info = (CommunicateInfo *)arg;
    int tmpfd=info->fd;
    struct pollfd *tmpfds=info->fds;
                
    // 接收数据，一次接收10个字节，客户端每次发送100个字节,下一轮select检测的时候, 内核还会标记这个文件描述符缓冲区有数据 -> 再读一次
    // 循环会一直持续, 知道缓冲区数据被读完位置
    char buf[10] = {0};
    int len = read(info->fd, buf, sizeof(buf));
    
    while (len)
    {
        if (len == 0) // 客户端关闭了连接，，因为如果正好读完，会在select过程中删除
        {
            printf("客户端关闭了连接.....\n");
            // 将该文件描述符从集合中删除
            pthread_mutex_lock(&mymutex);
            for(int i=0; i<1024; ++i)
            {
                if(info->fds[i].fd == info->fd)
                {
                    info->fds[i].fd = -1;
                    break;
                }
            }
            pthread_mutex_unlock(&mymutex);
            free(info);
            close(info->fd);
            return NULL;
        }
        else if (len > 0) // 收到了数据
        {
            // 发送数据
            write(info->fd, buf, strlen(buf) + 1);
            cout << "写了一次" << endl;
            //return NULL;
        }
        else
        {
            // 异常
            perror("read");
            for(int i=0; i<1024; ++i)
            {
                if(tmpfds[i].fd == tmpfd)
                {
                    tmpfds[i].fd = -1;
                    break;
                }
            }
            free(info);
            return NULL;
        }
        len = read(info->fd, buf, sizeof(buf));
        if (len == 0) // 客户端关闭了连接，，因为如果正好读完，会在select过程中删除
        {
            printf("客户端关闭了连接.....\n");
            // 将该文件描述符从集合中删除
            pthread_mutex_lock(&mymutex);
            for(int i=0; i<1024; ++i)
            {
                if(tmpfds[i].fd == tmpfd)
                {
                    tmpfds[i].fd = -1;
                    break;
                }
            }


            pthread_mutex_unlock(&mymutex);
            free(info);
            close(info->fd);
            return NULL;
        }
    }
    free(info);
    return NULL;
}

int main() // 基于多路复用select函数实现的并行服务器
{
    pthread_mutex_init(&mymutex, NULL);
    // 1 创建监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    // 2 绑定
    struct sockaddr_in addr; // struct sockaddr_in是用于表示IPv4地址的结构体，它是基于struct sockaddr的扩展。
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9997);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(lfd, (struct sockaddr *)&addr, sizeof(addr));

    // 3 设置监听
    listen(lfd, 128);

    // 将监听的fd的状态交给内核检测
    int maxfd = lfd;
    // 初始化检测的读集合
    struct pollfd myfd[100];
    for(int i=0;i<100;i++)
    {
        myfd[i].fd=-1;
        myfd[i].events=POLLIN;
    }
    myfd[0].fd=lfd;

    


    while (1)
    {
        sleep(2);

        cout << "開始等待" << endl;

    

        int num=poll(myfd,maxfd+1,-1);

        cout << "poll等待結束" << endl;

        // 判断连接请求还在不在里面，如果在，则运行accept
        if (myfd[0].revents & POLLIN)
        {

            // 添加一个子线程用于连接客户端
            pthread_t conn;
            //fdInfo *info = (fdInfo *)malloc(sizeof(fdInfo));
            fdInfo* info = new fdInfo;  // 创建一个非 const 的指针对象
            info->fd = lfd;
            info->maxfd = &maxfd;
            info->fds=&myfd[0];
            pthread_create(&conn, NULL, ConnAccept, info);
            pthread_detach(conn);
        }

        // 如果没有建立新的连接，那么就直接通信
        for (int i = 0; i < maxfd + 1; i++)
        {

            if (myfd[i].fd != lfd && myfd[i].revents==POLLIN  )
            {
                
                // 添加一个子线程用于通信
                cout<<"創建一個通訊"<<endl;
                pthread_t Communicate;
                //fdInfo *info = (fdInfo *)malloc(sizeof(fdInfo));
                CommunicateInfo* info = new CommunicateInfo;  // 创建一个非 const 的指针对象
                info->fd = myfd[i].fd;
                info->maxfd = &maxfd;
                info->fds=myfd;
                pthread_create(&Communicate, NULL, Communication, info);
                pthread_detach(Communicate);
            }
        }
    }

    pthread_mutex_destroy(&mymutex);
    return 0;
}