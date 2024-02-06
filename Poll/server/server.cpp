#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <iostream>
#include <poll.h>

using namespace std;

int main() // 基于多路复用select函数实现的并行服务器
{
    // 1 创建监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    // 2 绑定
    struct sockaddr_in addr; // struct sockaddr_in是用于表示IPv4地址的结构体，它是基于struct sockaddr的扩展。
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9995);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(lfd, (struct sockaddr *)&addr, sizeof(addr));

    // 3 设置监听
    listen(lfd, 128);

    // 将监听的fd的状态交给内核检测
    int maxfd = lfd;

    //创建文件描述符的队列
    struct pollfd myfd[100];
    for(int i=0;i<100;i++)
    {
        myfd[i].fd=-1;
        myfd[i].events=POLLIN;
    }

    myfd[0].fd=lfd;


    while (1)
    {
        //sleep(5);
        
        cout<<"poll等待开始"<<endl;
        int num=poll(myfd,maxfd+1,-1);
        cout<<"poll等待结束~"<<endl;

        // 判断连接请求还在不在里面，如果在，则运行accept

        if(myfd[0].fd && myfd[0].revents==POLLIN)
        {
            struct sockaddr_in cliaddr;
            int cliaddrLen = sizeof(cliaddr);
            int cfd = accept(lfd, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrLen);

            // 得到了有效的客户端文件描述符，将这个文件描述符放入读集合当中，并更新最大值
            for(int i=0 ; i<1024 ;i++)//找到空的位置
            {
                if(myfd[i].fd==-1 && myfd[i].events==POLLIN)
                {
                    myfd[i].fd=cfd;
                    cout<<"连接成功！      fd放在了"<<i<<endl;
                    break;
                }

            }
            maxfd = cfd > maxfd ? cfd : maxfd;
            
        }

        // 如果没有建立新的连接，那么就直接通信


        for (int i = 0; i < maxfd + 1; i++)
        {
            if (myfd[i].fd && myfd[i].revents==POLLIN && i!=0)
            {

                // 接收数据，一次接收10个字节，客户端每次发送100个字节,下一轮select检测的时候, 内核还会标记这个文件描述符缓冲区有数据 -> 再读一次
                //  	循环会一直持续, 知道缓冲区数据被读完位置
                char buf[10] = {0};
                cout<<"                  外读"<<endl;
                int len = read(myfd[i].fd, buf, sizeof(buf));
                cout<<"len="<<len<<"          i="<<i<<endl;
                if(len==0)  //外部中斷導致的連接中斷
                {
                    printf("客户端关闭了连接.....\n");
                    // 将该文件描述符从集合中删除
                    myfd[i].fd=-1;
                    break;
                }


                cout<<"Get read len="<<len<<endl;
                if (len == 0) // 客户端关闭了连接，，因为如果正好读完，会在select过程中删除
                {
                    printf("客户端关闭了连接.....\n");
                    // 将该文件描述符从集合中删除
                    myfd[i].fd=-1;
                    break;

                }
                else if (len > 0) // 收到了数据
                {
                    // 发送数据
                    if(len<=2)
                    {
                        cout<<"          out!!"<<endl;
                        break;
                    }
                    write(myfd[i].fd, buf, strlen(buf) + 1);
                    if(len<10)
                    {
                        cout<<"          out!!"<<endl;
                        break;
                    }
                    sleep(0.1);
                    cout<<"写了一次   寫的內容是："<<string(buf)<<"###"<<endl;

                }
                else
                {
                    // 异常
                    perror("read");
                    myfd[i].fd=-1;
                    break;
                }


            }

        }
    }

    return 0;
}