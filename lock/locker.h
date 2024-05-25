#ifndef LOCKER_H
#define LOCKER_H

#include<exception>//标准异常库
#include<pthread.h>
#include<semaphore.h>

//信号量
class sem{
    public:
        //默认构造函数
        sem(){
            //sem_init函数是一个POSIX函数，用于初始化信号量
            //如果sem_init 返回非0值，表示初始化失败，则抛出异常
            //sem_init第一个参数 指向要初始化的信号量的指针 第二个参数 0表示信号量只能被初始化它的进程的所有线程共享 非0则可以在多个进程之间共享  第三个参数信号量的初始值
            //信号量的初始技术为0
            if(sem_init(&m_sem,0,0)!=0){
                throw std::exception();
            } 
        }
        //重载构造函数
        sem(int num){
            if(sem_init(&m_sem,0,num)!=0){
                throw std::exception();
            }
        }
        //析构函数
        ~sem(){
            sem_destroy(&m_sem);//sem_destroy函数用于释放信号量资源  成功返回0 失败返回-1 失败原因存储在 error中
        }
        //等待函数
        bool wait(){
            return sem_wait(&m_sem)==0;//sem_wait用于减少信号量的计数值  成功返回0 失败返回-1 如果信号量的值为0，调用sem_wait的线程将被阻塞，直到信号量的值大于零
        }
        //发布函数
        bool post(){
            return sem_post(&m_sem)==0;//sem_post用于增加信号量的计数值 成功返回0 失败返回-1 如果有因为sem_wait进行阻塞的线程，sem_post会使其中一个线程解除阻塞状态
        }
    
    private:
        sem_t m_sem;
};

//互斥锁
class locker{
    private:
        pthread_mutex_t m_mutex;
    public:
        //构造函数
        locker(){
            if(pthread_mutex_init(&m_mutex,nullptr)!=0){//用pthread_mutex_init初始化m_mutex  成功返回0 失败返回非零
                throw std::exception();
            }
        }
        //析构函数
        ~locker(){
            pthread_mutex_destroy(&m_mutex);
        }
        //加锁
        bool lock(){
            return pthread_mutex_lock(&m_mutex)==0;
        }
        //解锁
        bool unlock(){
            return pthread_mutex_unlock(&m_mutex)==0;
        }
        //获取互斥锁指针的函数
        pthread_mutex_t *get(){
            return &m_mutex;
        }
};

//条件变量
class cond{
    private:
        //成员变量 m_cond 用于线程之间的同步
        pthread_cond_t m_cond;
    public:
        //构造函数
        cond(){
            if(pthread_cond_init(&m_cond,nullptr)!=0){
                throw std::exception();
            }
        }
        //析构函数
        ~cond(){
            pthread_cond_destroy(&m_cond);//释放与条件变量相关的资源
        }
        //等待条件变量
        //wait函数是调用线程在指定的互斥锁 m_mutex 上等待 m_cond条件变量，在此期间互斥锁会被释放，以允许其他线程修改条件
        //一旦条件满足并且线程被唤醒，互斥锁将重新被锁定
        //函数返回true 表示成功等待和重新锁定互斥锁
        bool wait(pthread_mutex_t *m_mutex){
            //pthread_cond_wait函数用于阻塞当前线程，直到指定的条件变量被触发
            //第一个参数 指向条件变量的指针
            //第二个参数 指向互斥锁的指针 这个互斥锁必须在调用“pthread_cond_wait”之前由调用线程锁定
            return pthread_cond_wait(&m_cond,m_mutex)==0;
        }
        //带超时的等待
        //允许线程在超过指定阻塞时间后，也能返回，避免永久阻塞
        //成功等待条件或达到超时都会返回true  失败返回false
        bool timewait(pthread_mutex_t *m_mutex,struct timespec t){
            //pthread_cond_timewait 第三个参数是 指向 timespec 结构的指针，该结构定义了等待的绝对超时时间
            return pthread_cond_timedwait(&m_cond,m_mutex,&t)==0;
        }
        //发送信号
        //signal函数用于唤醒等待同一条件变量的至少一个线程。如果有多个线程在等待，只有一个会被唤醒
        //函数返回true 表示成功发送信号
        bool signal(){
            //pthread_cond_signal函数 参数 指向条件变量的指针
            //此函数至少唤醒一个等待指定条件变量的线程  如果没有线程在等待，调用此函数没有任何效果
            return pthread_cond_signal(&m_cond)==0;
        }
        //广播信号
        //broadcast函数用于唤醒等待同一条件变量的所有线程
        //返回true表示成功广播信号
        bool broadcast(){
            //pthread_cond_broadcast函数 参数 指向条件变量的指针
            //此函数唤醒所有等待指定条件变量的线程  如果没有线程在等待，调用此函数没有任何效果
            return pthread_cond_broadcast(&m_cond)==0;
        }
};



#endif