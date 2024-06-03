#include "lst_timer.h"
#include "../http/http_conn.h"


//-------------------------------------------          sort_timer_lst类成员函数的实现          ----------------------------------------------------------//

//sort_timer_lst 构造函数
sort_timer_lst::sort_timer_lst(){
    head=nullptr;
    tail=nullptr;
}
//sort_timer_lst 析构函数  在sort_timer_lst实例销毁时，清理它所管理的util_timer定时器链表
sort_timer_lst::~sort_timer_lst(){
    util_timer *tmp=head;//从链表头部开始
    while(tmp){
        head=tmp->next;
        delete tmp;
        tmp=head;
    }
}

//add_timer  将一个util_timer定时器添加到定时器链表中
void sort_timer_lst::add_timer(util_timer *timer){
    if(!timer)return;//传入的定时器指针为空
    if(!head){
        head=tail=timer;//链表头为空，新的定时器既是头也是尾
        return;
    }
    //新定时器的时间最小
    if(timer->expire<head->expire){
        timer->next=head;
        head->prev=timer;
        head=timer;
        return;
    }
    //如果定时器的时间不是最小的，调用add_timer的重载版本
    add_timer(timer,head);
}
//add_timer 一个重载版本
void sort_timer_lst::add_timer(util_timer *timer,util_timer *lst_head){
    //初始化
    util_timer *prev=lst_head;//从提供的链表节点开始
    util_timer *tmp=prev->next;//获取链表头节点的下一个位置

    //遍历链表
    while(tmp){
        //找到了新定时器应该插入的位置
        if(timer->expire<tmp->expire){
            prev->next=timer;//将新定时器插入 prev 和 tmp 之间
            timer->next=tmp;
            tmp->prev=timer;
            timer->prev=prev;
            break;//完成插入退出循环
        }
        prev=tmp;
        tmp=tmp->next;
    }
    //如果达到链表尾部仍未找到插入点 那这个新定时器要成为新的链表尾部
    if(!tmp){
        prev->next=timer;
        timer->prev=prev;
        timer->next=nullptr;
        tail=timer;
    }
}

//adjuster_timer    在修改某个定时器的expire后，确保链表维持按到期时间从小到大的有序状态    
void sort_timer_lst::adjust_timer(util_timer *timer){
    if(!timer){
        return;//传入空的定时器，直接返回
    }
    util_timer *tmp=timer->next;//获取定时器的下一个节点
    if(!tmp||(timer->expire<tmp->expire)){
        return;//如果没有下一个定时器或定时器已经在正确的位置，不需要调整
    }
    if(timer==head){
        head=head->next;//如果定时器是头节点，更新头节点
        head->prev=nullptr;//清空新头节点的前指针
        timer->next=nullptr;//清空原头节点的下一个指针
        add_timer(timer,head);//重新插入定时器
    }else{
        //修改的定时器不是头节点
        timer->prev->next=timer->next;//断开定时器的前后节点连接
        timer->next->prev=timer->prev;
        add_timer(timer,timer->next);//在其原来位置的后面重新插入定时器
    }
}

//del_timer   在链表中删除某个定时器
void sort_timer_lst::del_timer(util_timer *timer){
    if(!timer)return;
    if((timer==head)&&(timer==tail)){
        delete timer;
        head=nullptr;
        tail=nullptr;
        return;
    }
    if(timer==head){
        head=head->next;
        head->prev=nullptr;
        delete timer;
        return;
    }
    if(timer==tail){
        tail=tail->prev;
        tail->next=nullptr;
        delete timer;
        return;
    }
    timer->prev->next=timer->next;
    timer->next->prev=timer->prev;
    delete timer;
}

//tick 检查链表中的定时器，并执行那些已经到期的定时器的回调函数
void sort_timer_lst::tick(){
    if(!head)return;//链表为空
    time_t cur=time(nullptr);//获取当前时间
    util_timer *tmp=head;//从链表头节点开始检查
    while(tmp){
        if(cur<tmp->expire)break;//当前定时器未到期，停止检查
        tmp->cb_func(tmp->user_data);//调用到期定时器的回调函数
        head=tmp->next;//移动头指针到下一个定时器
        if(head)head->prev=nullptr;//清除头节点的前指针
        delete tmp;//删除到期的定时器
        tmp=head;//继续检查下一个定时器
    }
}


//----------------------------------------------------     Utils类成员函数实现     ---------------------------------------------------------

//init 设置时间间隔
void Utils::init(int timeslot){
    m_TIMESLOT=timeslot;//设置时间间隔
}

//setnonblocking 将指定的文件描述符fd设置为非阻塞模式   对于事件驱动模型，非阻塞模式可以提高程序的效率和响应能力
int Utils::setnonblocking(int fd){
    int old_option=fcntl(fd,F_GETFL);//获取fd的当前文件状态标志
    int new_option=old_option|O_NONBLOCK;//向当前标志添加非阻塞标志
    fcntl(fd,F_SETFL,new_option);//设置新的标志到fd
    return old_option;//返回原始的文件状态标志
}

//addfd 将文件描述符fd注册到由epollfd指定的epoll事件表中，并配置相关的事件类型和触发模式
void Utils::addfd(int epollfd,int fd,bool one_shot,int TRIGMode){
    epoll_event event;
    event.data.fd=fd;//设置事件相关联的文件描述符

    if(TRIGMode==1){
        event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;//设置为边缘触发模式
    }else{
        event.events=EPOLLIN|EPOLLRDHUP;//设置水平触发模式
    }

    if(one_shot)event.events|=EPOLLONESHOT;//如果指定one_shot，添加EPOLLONESHOT到事件类型中
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);//注册fd到epollfd指定的epoll实例
    setnonblocking(fd);//设置fd为非阻塞模式
}

//sig_handler 信号处理函数
void Utils::sig_handler(int sig){
    int save_errno=errno;//保存当前的errno，以保证可重入性
    int msg=sig;//将接收到的信号值保存到msg
    send(u_pipefd[1],(char *)&msg,1,0);//将信号值发送到管道，通知主循环
    errno=save_errno;//恢复errno
}

//addsig 配置特定信号处理方式的函数
void Utils::addsig(int sig, void(handler)(int),bool restart){
    struct sigaction sa;//定义sigaction结构体以配置信号处理
    memset(&sa,'\0',sizeof(sa));//初始化sigaction结构体为零
    sa.sa_handler=handler;//设置信号处理函数
    if(restart)sa.sa_flags|=SA_RESTART;//设置SA_RESTART标志
    sigfillset(&sa.sa_mask);//在sa_mask中填充所有信号，阻塞所有信号处理过程中的其他信号
    assert(sigaction(sig,&sa,nullptr)!=-1);//设置信号sig的处理方式，断言不失败
}

//timer_handler  既触发当前到期的定时器任务，也为未来的定时任务重新设定定时器
void Utils::timer_handler(){
    m_timer_lst.tick();//处理所有到期的定时器任务
    alarm(m_TIMESLOT);//重新设置定时器，以继续触发SIGALRM信号
}

//show_error  用于向客户端显示错误信息并关闭连接
void Utils::show_error(int connfd,const char *info){
    //confd:客户端的连接文件描述符，通过它服务器可以向特定的客户端发送数据
    //info：一个指向错误信息字符串的指针
    send(connfd,info,strlen(info),0);//向连接发送错误信息
    close(connfd);//关闭这个连接
}

//Utils静态成员变量初始化
int *Utils::u_pipefd=0;
int Utils::u_epollfd=0;

class Utils;//声明Utils类

//cb_func 回调函数 负责从epoll监控中移除对应得socket文件描述符，并关闭连接 同时也处理与连接相关的一些应用状态，如减少用户计数器
void cb_func(client_data *user_data){
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);//从epoll实例中移除socket文件描述符
    assert(user_data);//断言以确保user_data指针不为空  如果user_data为nullptr，assert将导致程序中断，有助于早期发现问题
    close(user_data->sockfd);//关闭socket文件描述符
    http_conn::m_user_count--;//减少应用层维护的用户计数
}