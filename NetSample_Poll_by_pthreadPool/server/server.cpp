#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <iostream>
#include <pthread.h>
#include "ThreadPool.hpp"
#include <poll.h>

using namespace std;

pthread_mutex_t mymutex;

typedef struct fdInfo
{
    int fd;
    int *maxfd;
    socklen_t *cliaddr;
    struct pollfd *fds;
} fdInfo;

typedef struct CommunicateInfo
{
    int fd;
    int *maxfd;
    struct pollfd *fds;
} CommunicateInfo;

void ConnAccept(void *arg)
{
    fdInfo *info = (fdInfo *)arg;

    int cliaddrLen = sizeof(info->cliaddr);
    int cfd = accept(info->fd, (struct sockaddr *)&info->cliaddr, (socklen_t *)&cliaddrLen);
    cout << "連接成功！ " << endl;
    
    // 得到了有效的客户端文件描述符，将这个文件描述符放入读集合当中，并更新最大值
    pthread_mutex_lock(&mymutex);
    for(int i=0;i<100;i++)
    {
        if(info->fds[i].fd==-1)
        {
            info->fds[i].fd=cfd;
            break;
        }
    }

    *info->maxfd = cfd > *info->maxfd ? cfd : *info->maxfd;
    pthread_mutex_unlock(&mymutex);
    cout << "设置成功！ " << endl;
    free(info);

    return ;
}

void Communication(void *arg)
{
    cout << "開始通信" << endl;
    CommunicateInfo *info = (CommunicateInfo *)arg;
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
            for(int i=0;i<100;i++)
            {
                if(info->fds[i].fd==info->fd)
                {
                    info->fds[i].fd=-1;
                    break;
                }
            }
            pthread_mutex_unlock(&mymutex);
            free(info);
            close(info->fd);
            return ;
        }
        else if (len > 0) // 收到了数据
        {
            // 发送数据
            write(info->fd, buf, strlen(buf) + 1);
            cout << "写了一次" << endl;
            //return ;
        }
        else
        {
            // 异常
            perror("read");
            // 将该文件描述符从集合中删除
            pthread_mutex_lock(&mymutex);
            for(int i=0;i<100;i++)
            {
                if(info->fds[i].fd==info->fd)
                {
                    info->fds[i].fd=-1;
                    break;
                }
            }
            pthread_mutex_unlock(&mymutex);
            free(info);
            return ;
        }
        len = read(info->fd, buf, sizeof(buf));
        if (len == 0) // 客户端关闭了连接，，因为如果正好读完，会在select过程中删除
        {
            printf("客户端关闭了连接.....\n");
            // 将该文件描述符从集合中删除
            pthread_mutex_lock(&mymutex);
            for(int i=0;i<100;i++)
            {
                if(info->fds[i].fd==info->fd)
                {
                    info->fds[i].fd=-1;
                    break;
                }
            }
            pthread_mutex_unlock(&mymutex);
            free(info);
            close(info->fd);
            return ;
        }
    }
    free(info);
    return ;
}

int main() // 基于多路复用select函数实现的并行服务器
{

    ThreadPool pool(1,8);
    cout<<"线程池初始化完毕............................."<<endl;
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
    
    //初始化poll所需的文件描述符数组
    struct pollfd myfd[100];
    for(int i=0;i<100;i++)
    {
        myfd[i].fd=-1;
        myfd[i].events=POLLIN;
    }
    
    myfd[0].fd=lfd;



    while (1)
    {
        sleep(1);
        //差开始通讯的循环

        cout << "開始等待" << endl;

        //timeval *RunoutTime;
        //RunoutTime->tv_sec=1;
        //RunoutTime->tv_usec=0;
        int num = poll(myfd,maxfd+1,-1);
        //cout<<"               rdtemp="<<rdtemp<<endl;

        cout << "pll等待結束" << endl;

        // 判断连接请求还在不在里面，如果在，则运行accept
        if (myfd[0].revents & POLLIN)
        {
            cout<<"开始分配连接请求处理进程"<<endl;
            // 添加一个子线程用于连接客户端
            pthread_t conn;
            fdInfo *info = (fdInfo *)malloc(sizeof(fdInfo));
            info->fd = lfd;
            info->maxfd = &maxfd;
            info->fds=&myfd[0];
            //pthread_create(&conn, NULL, ConnAccept, info);
            //pthread_detach(conn);
            Task conTask;
            conTask.function=ConnAccept;
            conTask.arg=info;
            pool.addTask(conTask);
        }

        // 如果没有建立新的连接，那么就直接通信
        for (int i = 0; i < maxfd + 1; i++)
        {
            if (i != 0 && myfd[i].revents & POLLIN )
            {
                //myfd[i].revents=NULL;

                cout<<"开始分配工作线程,线程位置为:"<<i<<endl;
                // 添加一个子线程用于通信
                pthread_t Communicate;
                CommunicateInfo *info = (CommunicateInfo *)malloc(sizeof(CommunicateInfo));
                info->fd = myfd[i].fd;
                info->maxfd = &maxfd;
                info->fds=&myfd[0];
                //pthread_create(&Communicate, NULL, Communication, info);
                //pthread_detach(Communicate);
                Task CommuTask;
                CommuTask.function=Communication;
                CommuTask.arg=info;

                pool.addTask(CommuTask);
                myfd[i].fd=-1;
            }
            else
            {
                cout<<"         跳过分配工作"<<endl;
            }
        }
    }

    pthread_mutex_destroy(&mymutex);
    return 0;
}