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