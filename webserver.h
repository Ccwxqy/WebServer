#ifndef WEBSERVER_H
#define WEBSERVER_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>
#include"./threadpool/threadpool.h"
#include"./http/http_conn.h"

const int MAX_FD=65536;//最大文件描述符
const int MAX_EVENT_NUMBER=10000;//最大事件数
const int TIMESLOT=5;//最小超时单位

class webserver{
    //成员变量
    public:
        //基础
        int m_port; //存储服务器监听得端口号  Web服务器通过此端口接收客户端的连接请求
        char *m_root; //字符指针  指向服务器的根目录路径。这个路径通常用来存放服务器可以提供的资源 如HTML文件
        int m_log_write;//指示日志的写入方式  异步/同步
        int m_close_log;//是否关闭日志
        int m_actormodel;//表示服务器采用的并发模型
        int m_epollfd;//epoll事件轮询机制中的一个文件描述符  管来所有的网络连接和其他需要异步处理的事件
        int m_pipefd[2];//一个整形数组，用于创建管道  pipefd[0]用于读  pipefd[1]用于写
        http_conn *users;//指向http_conn类对象的指针数组，每个对象代表一个客户端连接 http_conn类负责处理HTTP连接和请求  这个数组允许Web服务器管理多个并发客户端，每个连接都由一个http_conn对象处理

        //数据库相关
        connection_pool *m_connPool;//一个指向数据库连接池的指针
        string m_user;//存储数据库的登陆用户名    在初始化数据库连接或执行数据库操作时，这个用户名将被用来验证用户的身份，以确保安全访问数据库
        string m_passWord;//存储用于访问数据库的密码
        string m_databaseName;//这个变量指定服务器应该连接和操作的数据库名称  指定数据库名是连接数据库时必需的，它告诉服务器哪一个具体的数据库实例将被用于存储和检索数据
        int m_sql_num;//表示连接池中维护的数据库连接数量  通过设置这个数量，可以控制服务器与数据库之间并发连接的最大限度，从而优化性能和资源使用

        //线程池相关
        threadpool<http_conn> *m_pool;//一个指向threadpool模板类的指针，模板参数是http_conn类型 这意味着线程池是专门用来管理http_conn对象的工作线程
        int m_thread_num;//存储线程池中线程的数量 这个数字决定了服务器可以同时处理的并发HTTP请求的上限

        //epoll_event相关
        epoll_event events[MAX_EVENT_NUMBER];//events数组用于存储epoll处理的事件，这些事件可以是连接请求 数据读取 数据写入等 
        int m_listenfd;//服务器监听的文件描述符，用于接收新的客户端连接。这是服务器开始接收客户端请求的起点，epoll会监控这个描述符来识别新的连接请求
        int m_OPT_LINGER;//用于设置控制套接字的SO_LINGER选项，这个选项影响关闭套接字时的行为。如果启用，并设置了适当的延时，关闭操作会等待数据完全发送或超时后才完成，有助于确保数据不会在连接突然关闭时丢失
        int m_TRIGMode;//服务器使用的触发模式
        int m_LISTENTrigmode;//专门控制监听套接字的触发模式。它决定了服务器如何响应新的连接请求--是仅在新连接到达时通知一次(边缘触发),还是只要有连接请求就持续通知(水平触发)
        int m_CONNTrigmode;//控制已连接套接字的触发模式

        //定时器相关
        client_data *users_timer;//指向client_data类型的指针，用于存储与每个客户端连接相关的数据 包括客户端的socket文件描述符 地址信息 以及与该连接相关的定时器对象
        Utils utils;//实用功能，包含处理超时事件 调整定时器等实用功能


    //成员函数
    public:
        webserver();
        ~webserver();
        void init(int port, string users,string passWord, string databaseName,                  
                    int log_write, int opt_linger, int trigmode,
                    int sql_num, int thread_num, int close_log ,int actor_model); //初始化服务器的基本设置，包括端口号 数据库登陆凭证 日志设置 连接模式等
        void thread_pool();//初始化或管理线程池  涉及创建线程 分配任务给线程操作
        void sql_pool();//初始化数据库连接池，确保有足够的数据库连接可提供服务器使用 提高数据库操作的效率
        void log_write();//处理日志写入操作 根据配置(同步/异步) 涉及将日志数据写入文件或其他日志存储机制
        void trig_mode();//设置和管理epoll的触发模式
        void eventListen();//设置监听套接字，并将其添加到epoll监控事件中，准备接收客户端连接
        void eventLoop();//开始事件循环，处理所有通过epoll捕获的事件 包括连接请求 数据读写等
        void timer(int connfd, struct sockaddr_in client_address);//为新的客户端连接设置定时器 用于管理连接的超时，如客户端在指定时间内没有活动，则自动断开连接
        void adjust_timer(util_timer *timer);//调整定时器的设置
        void deal_timer(util_timer *timer,int sockfd);//处理超时事件 通常是断开超时的连接 释放相关资源
        bool dealclientdata();//处理客户端发送的数据。包括读取数据 解析请求
        bool dealwithsignal(bool& timeout,bool& stop_server);//处理信号事件 如定时信号或终止信号
        void dealwithread(int sockfd);//处理读事件 即从指定的套接字读取数据
        void dealwithwrite(int sockfd);//处理写事件 即向指定的套接字发送数据
};
#endif