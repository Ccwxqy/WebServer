/*
循环数组实现的阻塞队列，m_back=(m_back+1)%m_max_size;
线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<sys/time.h>
#include"../lock/locker.h"
using namespace std;


//定义 一个基于数组的 阻塞队列模板类
template<class T>
class block_queue{
    //成员变量
    private:
        locker m_mutex;//互斥锁 用于同步多线程对队列的访问
        cond m_cond;//条件变量，用于线程间的同步
        T *m_array;//动态数组，存储队列元素
        int m_size;//队列当前元素数量
        int m_max_size;//队列最大容量
        int m_front;//队列头部的索引
        int m_back;//队列尾部的索引

    public:
        //构造函数
        //参数默认值1000，没有指定max_size则队列的最大容量将被设置为1000
        block_queue(int max_size=1000){
            if(max_size<=0){
                exit(-1);
            }
            m_max_size=max_size;//设置队列的最大容量
            m_array=new T[max_size];//动态分配一个大小为max_size的数组，用于存储队列元素 存储类型为模板参数T的对象
            m_size=0;//初始化队列的当前大小为0，表示队列开始时是空的
            m_front=-1;//初始化队列的前端索引为-1，表示队列是空的
            m_back=-1;//初始化队列的后端索引为-1，表示队列是空的
        }
        //析构函数
        ~block_queue(){
            //加锁，确保在多线程环境中没有其他线程正在访问这些资源
            m_mutex.lock();
            //资源释放
            if(m_array!=nullptr)delete[] m_array;
            //解锁
            m_mutex.unlock();
        }
        //重置队列状态 使其变为空队列
        void clear(){
            //加锁
            m_mutex.lock();
            //重置队列状态
            //将队列的大小设为0，表示队列中不再有元素
            m_size=0;
            m_front=-1;
            m_back=-1;
            //解锁
            m_mutex.unlock();
        }
        //判断队列是否满了
        bool full(){
            //加锁
            m_mutex.lock();
            //当前队列元素个数大于等于队列最大容量，说明队列已经满了
            if(m_size>=m_max_size){
                //解锁
                m_mutex.unlock();
                return true;
            }
            //解锁
            m_mutex.unlock();
            return false;
        }
        //判断队列是否为空
        bool empty(){
            m_mutex.lock();
            //当前队列元素个数为0，表面队列为空
            if(m_size==0){
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        }
        //返回队首元素
        bool front(T &value){//引用传递
            m_mutex.lock();
            if(m_size==0){
                m_mutex.unlock();
                return false;
            }
            //访问队首元素，获取队首元素的值并将其赋给value
            value=m_array[m_front];
            m_mutex.unlock();
            return true;
        }
        //返回队尾元素
        bool back(T &value){
            m_mutex.lock();
            if(m_size==0){
                m_mutex.unlock();
                return false;
            }
            //获取队尾元素，并将其赋给value
            value=m_array[m_back];
            m_mutex.unlock();
            return true;
        }
        //返回队列元素个数
        int size(){
            int tmp=0;
            m_mutex.lock();
            tmp=m_size;
            m_mutex.unlock();
            return tmp;
        }
        //返回队列最大容量
        int max_size(){
            int tmp=0;
            m_mutex.lock();
            tmp=m_max_size;
            m_mutex.unlock();
            return tmp;
        }
        //往队列添加元素，需要将所有使用队列的线程先唤醒   当有元素push进队列，相当于生产者生产了一个元素  若当前没有线程等待条件变量，则唤醒无意义
        //形参使用const T &    使用&传递参数不需要为传递的对象创建一个新的副本，避免复制对象时的开销
        //const保持参数不可修改，确保函数内部不能修改传入的参数，有助于保护数据
        //使用 const T& 使得函数可以接受临时对象和常量作为参数
        bool push(const T &item){
            //在队列进行修改前先锁定互斥锁
            m_mutex.lock();
            //检查队列是否已满
            if(m_size>=m_max_size){
                //如果队列已经是满的，则发出广播，通知其他正在等待的线程
                m_cond.broadcast();
                m_mutex.unlock();
                return false;
            }
            //添加元素到队列
            m_back=(m_back+1)%m_max_size;//更新m_back索引到下一个位置，如果到达数组末尾则循环回到数组开始的位置
            m_array[m_back]=item;//在新的m_back位置存储元素item
            m_size++;//增加队列的大小

            //通知其他线程
            m_cond.broadcast();

            //解锁
            m_mutex.unlock();
            return true;
        }
        //pop时，如果当前队列没有元素，将会等待条件变量
        bool pop(T &item){
            m_mutex.lock();
            //等待条件满足
            while(m_size<=0){//如果队列没有元素，那就一直等待，直到队列中有元素可供pop
                //调用 wait 方法等待条件变量被触发(即队列中有元素被添加) m_size>0 退出循环
                //如果等待失败，则解锁互斥锁并返回false
                if(!m_cond.wait(m_mutex.get())){
                    m_mutex.unlock();
                    return false;
                }
            }
            //移除队首元素
            m_front=(m_front+1)%m_max_size;//更新队首索引m_front 使用模运算保持循环队列的特性
            item=m_array[m_front];//从队列中取出队首元素并赋值给item参数
            m_size--;//队列大小减一
            //解锁
            m_mutex.unlock();
            return true;
        }
        //增加超时处理
        bool pop(T &item,int ms_timeout){
            ////时间管理
            //定义时间结构
            struct timespec t={0,0};//定义一个timespec结构，用于表示一个时间点，包括秒 tv_sec 和 纳秒 tv_nsec
            struct timeval now={0,0};//定义了一个timeval结构，用于获取当前时间，包括秒和微秒
            //获取当前时间
            gettimeofday(&now,nullptr);//获取当前时间，把当前时间存储在 now变量中
            //条件变量与超时等待
            m_mutex.lock();
            if(m_size<=0){//如果队列为空，进入等待状态
                //设置超时时间
                t.tv_sec=now.tv_sec+ms_timeout/1000;     //设置超时的秒数  ms_timeout/1000 将毫秒转换为秒
                t.tv_nsec=(ms_timeout%1000)*1000000;     //设置超时的纳秒数 (ms_timeout%1000)得到剩余的毫秒数，然后乘以1000000转换为纳秒 
                //条件变量的带超时等待
                if(!m_cond.timewait(m_mutex.get(),t)){
                    m_mutex.unlock();
                    return false;
                }   
            }
            //防止假唤醒  超过设置时间 timewait函数也返回true
            if(m_size<=0){
                m_mutex.unlock();
                return false;
            }
            //移除队首元素
            m_front=(m_front+1)%m_max_size;
            item=m_array[m_front];
            m_size--;
            m_mutex.unlock();
            return true;
        }
};

#endif