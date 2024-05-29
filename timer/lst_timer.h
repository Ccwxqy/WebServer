#ifndef LST_TIMER
#define LST_TIMER

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/wait.h>
#include<sys/uio.h>
#include<time.h>
#include"../log/log.h"

class util_timer;//声明定时器类

//定义client_data的结构体，用于存储特定客户端的数据
struct client_data
{
    sockaddr_in address;//存储了客户端的网络地址信息
    int sockfd;//网络套接字
    util_timer *timer;//一个指向utl_timer类型的指针，
};


//定时器类 util_timer
class util_timer{
    //成员变量
    public:
        time_t expire;//表示定时器的过期时间
        client_data *user_data;//指向与定时器相关联的客户端数据的指针
        util_timer *prev;//指向双向链表中前一个定时器的指针
        util_timer *next;//指向双向链表中下一个定时器的指针

    //成员函数
    public:
        util_timer():prev(nullptr),next(nullptr){};//构造函数   
        void (*cb_func)(client_data *);//函数指针，指向一个接收client_data类型指针作为参数的函数
};


//一个管理utl_timer对象的有序双向链表类 
class sort_timer_lst{
    //成员变量
    private:
        util_timer *head;//指向链表头部的指针  链表中的第一个定时器通常是最近到期的
        util_timer *tail;//指向链表尾部的指针  链表中最后一个定时器通常是最远到期的

    //成员函数
    public:
        sort_timer_lst();//用于初始化链表，设置头尾指针head 和tail 为nullptr 表示初始化时链表为空
        ~sort_timer_lst();//负责清理链表，释放所有定时器对象，防止内存泄漏

        void add_timer(util_timer *timer);//将新的定时器添加到链表中
        void adjust_timer(util_timer *timer);//调整现有定时器在链表中的位置
        void del_timer(util_timer *timer);//从链表中删除指定的定时器
        void tick();//心跳函数 定期调用以检查链表头部的定时器是否到期  到期的定时器会触发其回调函数，并从链表中移除

    private:
        void add_timer(util_timer *timer,util_timer *lst_head);//在给定的列表节点后插入新的定时器
};


//实用工具类Utils
class Utils{
    //静态成员变量
    public:
        static int *u_pipefd;//指向管道文件描述符数组的指针，用于进程间通信
        static int u_epollfd;//存储epoll事件处理的文件描述符
    //成员变量
    public:
        sort_timer_lst m_timer_lst;//定时器列表 用于管理所有定时器事件
        int m_TIMESLOT;//存储定时器触发的时间间隔

    //成员函数
    public:
        Utils(){};//构造函数
        ~Utils(){};//析构函数

    public:
        void init(int timelot);//初始化定时器触发间隔
        int setnonblocking(int fd);//将指定的文件描述符fd设置为非阻塞模式
        void addfd(int epollfd,int fd, bool one_shot,int TRIGMode);//将文件描述符注册到epoll事件监测表里
        void addsig(int sig, void(handler)(int),bool restart=true);//为特定的信号sig设置信号处理函数handler  如果restart为true 则在信号处理函数执行后重新启动被该信号中断的系统调用
        void timer_handler();//处理定时事件 用于重新设置定时器以触发SIGALRM信号，从而进行周期性的任务处理
        void show_error(int connfd,const char *info);//向连接标识符connfd对应的客户端显示错误信息info

        static void sig_handler(int sig);//静态信号处理函数  用于响应不同的信号sig 
};

//回调函数
void cb_func(client_data *user_data);
#endif




