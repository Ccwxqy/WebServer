#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


//线程池的类模板定义
template<typename T>
class threadpool{
    public:
        //thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量
        threadpool(int actor_model,connection_pool *connPool,int thread_number=8,int max_request=10000);
        ~threadpool();
        //向工作队列中添加任务
        bool append(T *request,int state);//添加带有特定状态的任务
        bool append_p(T *request);//添加优先任务 或特殊处理的任务

    private:
        //工作线程运行的函数，它不断从工作队列中取出任务并执行
        static void *worker(void *arg);//静态成员函数，作为线程启动的函数。这个函数从工作队列中持续取出任务并执行
        void run();//在每个线程中循环执行的函数，调用worker函数

    private:
        int m_thread_number;//线程池中的线程数     指示在线程池初始化时应该创建多少个线程来处理任务
        int m_max_requests;//请求队列中允许的最大请求数

        //pthread_t是POSIX线程库中用于标识和管理单个线程的数据类型
        //m_threads数组用于存储每个线程的pthread_t标识符，这允许线程池管理和控制其内部的每个线程，例如启动、停止和同步线程
        pthread_t *m_threads;//描述线程池的数组，其大小为 m_thread_number    
        std::list<T *>m_workqueue;//请求队列
        locker m_queuelocker;//保护请求队列的互斥锁
        sem m_queuestat;//是否有任务需要处理
        connection_pool *m_connPool;//数据库
        int m_actor_model;//模型切换

};


//模板类构造函数实现
template<typename T>//初始化列表
threadpool<T>::threadpool(int actor_model,connection_pool *connPool,int thread_number,int max_requests):m_actor_model(actor_model),m_thread_number(thread_number),m_max_requests(max_requests),m_threads(nullptr),m_connPool(connPool){
    //参数验证
    if(thread_number<=0||max_requests<=0){
        throw std::exception();//抛出异常
    }
    //线程数组分配，使用new操作符为线程标识符数组分配内存，大小为m_thread_number
    m_threads=new pthread_t[m_thread_number];
    //分配失败（m_threads为nullptr）抛出异常
    if(!m_threads){
        throw std::exception();
    }
    //线程创建并初始化
    for(int i=0;i<thread_number;++i){
        //pthread_create函数用于创建新线程，需要四个参数
        //第一个参数 m_threads+i 是指向pthread_t 结构的指针，该结构用于存储新线程的标识符，这里通过m_threads+i 传递当前迭代线程的地址
        //第二个参数 nullptr 表示新线程将使用默认的属性
        //第三个参数 worker 是一个函数指针，指向将被新线程执行的函数
        //第四个参数 this 是传递给 worker函数的参数，这里传递 threadpool 类的this指针，允许worker函数访问线程池类的成员
        //如果pthread_create函数返回非零值，表示线程创建失败
        if(pthread_create(m_threads+i,nullptr,worker,this)!=0){
            //创建失败 删除之前分配的m_threads 数组 并抛出异常 防止内存泄漏
            delete[] m_threads;
            throw std::exception();
        }

        //pthread_detach函数用于将指定的线程设置为 分离 状态，意味着线程结束时其占用的资源会自动被回收，不需要其他线程来回收，避免线程完成工作后变成僵尸线程
        //pthread_detach函数返回非零值，表示线程分离失败
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

//模板类析构函数
template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;//防止内存泄漏
}

//成员函数append函数实现 用于将任务添加到线程池的工作队列中
//T *request 一个指向任务的指针，这个任务应该是一个已定义的类型 T 的对象，该类型必须有一个成员变量 m_state
//int state 一个整数，用于设置任务的状态
template<typename T>
bool threadpool<T>::append(T *request,int state){
    //锁定线程池的工作队列，以防止多线程环境中的数据竞争
    m_queuelocker.lock();
    //检查队列是否已满
    if(m_workqueue.size()>=m_max_requests){
        //当前工作队列的大小已经达到或超过允许的最大请求量m_max_requests 无法加入任务
        m_queuelocker.unlock();
        return false;
    }
    //设置任务状态并添加到队列
    request->m_state=state;//在确认队列未满后，将传入的任务的状态设置为给定的state
    m_workqueue.push_back(request);//将任务添加到工作队列的末尾

    //解锁和通知
    m_queuelocker.unlock();//释放对队列的控制，允许其他线程访问队列
    m_queuestat.post();//调用post函数以增加信号量m_queuestat（加一） 这个信号量的作用是通知工作线程有新的任务可以处理   每次post操作都可能唤醒一个因等待任务而阻塞的线程
    
    return true;
}

//成员函数append_p函数实现，用于将任务添加到线程池的工作队列中
//append_p和append函数不同，append_p函数不设置任务的状态
template<typename T>
bool threadpool<T>::append_p(T *request){
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//模板类静态成员函数 worker 函数实现，该函数是每个线程的入口点，主要目的是从传入的参数中获取线程池对象的引用，并调用它的 run 方法来开始执行任务
//worker 作为一个静态成员函数，不绑定到任何特定的threadpool 实例，因此可以作为线程的启动例程
//接受一个 void* 类型的参数，这允许将任意类型的数据传递给线程函数
template<typename T>
void *threadpool<T>::worker(void *arg){
    //获取线程池对象
    //arg 参数被转换回 threadpool 类的指针。这是因为在创建线程时，通常传递线程池的 this 指针作为参数给 pthread_create 函数。
    //这样每个线程都知道它属于哪个线程池，并能访问线程池的公共和私有成员
    threadpool *pool=(threadpool *)arg;
    //执行线程池的run 方法
    pool->run();
    //返回pool指针，用于传递线程的退出信息
    return pool;
}

//模板类成员函数 run 函数实现  负责从工作队列中不断取出任务并执行
template<typename T>
void threadpool<T>::run(){
    //无限循环 意味着工作线程会一直运行 直到外部某种方式显示终止
    while(true){
        //等待新任务
        //使用信号量 m_queuestat 的 wait 方法等待新的任务。如果任务队列为空，工作线程将在这里阻塞，知道有新任务被添加到队列 并通过信号量被唤醒
        m_queuestat.wait();
        //锁定任务队列,保证多线程环境下是安全的
        m_queuelocker.lock();
        //检查队列是否为空
        if(m_workqueue.empty()){//在锁定检查队列是否为空时，理论上之前 wait方法的调用已经确保了队列不是空的，为了健壮性还是进行了检查  如果队列为空，则解锁并继续下一次循环
            m_queuelocker.unlock();
            continue;
        }

        //取出任务
        T *request=m_workqueue.front();//从队列前端取出一个任务
        m_workqueue.pop_front();//从队列中移除该任务
        //解锁互斥锁 允许其他线程访问队列
        m_queuelocker.unlock();

        //检查任务是否为空
        if(!request)continue;//如果取出的任务是空指针，则跳过当前循环

        //处理任务
        //根据m_actor_model 的值决定任务的处理方式
        //如果m_actor_model 为1，根据任务的 m_state 决定是调用 read_once 还是 write 方法
        if(m_actor_model==1){
            //执行任务
            //m_state==0 表示任务需要读取操作  如果read_once 成功，则进行数据库操作和进一步处理，如果失败，设置标志以示失败
            if(request->m_state==0){
                if(request->read_once()){//读取成功
                    request->improv=1;
                    connectionRAII mysqlcon(&request->mysql,m_connPool);
                    request->process();
                }else{//读取失败
                    request->improv=1;
                    request->timer_flag=1;
                }

            }else{// m_state!=0 表示任务需要执行写操作  如果 write成功，则进行相应标志的设置
                if(request->write()){//写入成功
                    request->improv=1;
                }else{//写入失败
                    request->improv=1;
                    request->timer_flag=1;
                }

            }
        }else{
            //m_actor_model!=1 执行默认处理流程，创建数据库连接并调用任务的 process 方法处理请求
            connectionRAII mysqlcon(&request->mysql,m_connPool);
            request->process();
        }
    }
}
#endif