#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>//实现可变参数
#include<pthread.h>
#include"block_queue.h"

using namespace std;
//日志类
class Log{
    private:
        //路径和文件名
        char dir_name[128];//路径名
        char log_name[128];//log文件名
        //日志参数
        int m_split_lines; //日志最大行数
        int m_log_buf_size; //日志缓冲区大小
        int m_today; //日志记录按天分类，记录当前时间是哪一天
        long long m_count; //日志行数记录
        //文件指针和缓冲区
        FILE *m_fp; //文件指针，指向当前的日志文件
        char *m_buf; //用于存储日志信息的缓冲区
        //日志队列和同步机制
        block_queue<string> *m_log_queue; //用于存储待写入的日志条目
        bool m_is_async; //表示日志是否以异步方式写入
        int m_close_log;//用于控制是否停止日志功能
        locker m_mutex;  //互斥锁

    public:
        //get_instance 静态函数，用于实现单例模式。 该函数通过定义一个静态局部变量 instance 来确保只创建一个Log类的实例。
        //调用此函数返回这个单一的实例的地址 
        //C++11开始，局部静态变量的初始化是线程安全的，不需要额外的锁机制
        static Log *get_instance(){
            static Log instance;//定义静态局部变量 instance
            return &instance;
        }
        //静态函数  用于创建一个线程，该线程执行 async_write_log 函数以异步方式将日志写入文件
        //在多线程的上下文中，线程启动函数必须有一个返回类型为 void* 并接受一个 void* 参数的签名。 这允许线程函数接受一个指向任何数据类型的指针，从而提供了很大的灵活性
        static void *flush_log_thread(void *args){
            Log::get_instance()->async_write_log();
            return nullptr;
        }
        //初始化日志系统 可以指定日志文件名 是否关闭日志 日志缓冲区大小 文件分割的最大行数 日志队列的最大大小
        bool init(const char *file_name,int close_log,int log_buf_size=8192,int split_lines=5000000,int max_queue_size=0);
        //写日志的函数 支持格式化字符串和可变参数列表 level可以用来指定日志级别（如INFO WARNING ERROR）
        //const char *format 这是一个格式字符串，它指定了随后可变参数的类型和如何格式化这些参数
        //...可变参数 允许函数接收任意数量和类型的参数，其数量和类型由 format 字符串指定
        void write_log(int level,const char *format,...);
        //用于立刻将缓冲区中的日志数据写入文件
        void flush(void);

    private:
        //构造函数 是私有的 确保只能通过 get_instance 创建实例
        Log();
        //虚析构函数 确保对象被正确地销毁
        virtual ~Log();
        //异步写日志函数，从阻塞队列中获取日志条目并写入文件 使用互斥锁 m_mutex 确保写入操作的线程安全
        //返回一个 void* 类型的指针，用于线程函数，以符合线程库对线程函数签名的要求
        void *async_write_log(){
            string single_log;//用于存储从队列中取出的单条日志信息
            //循环执行 直到pop方法返回false 
            //pop 方法从 m_log_queue阻塞队列中取出队首的日志条目并存放到single_log中
            //如果队列为空 pop方法会阻塞，直到有新的日志条目加入到队列中
            while(m_log_queue->pop(single_log)){
                ////日志写入和同步
                m_mutex.lock();//锁定代码块，确保同时只有一个线程可以执行文件写入操作   因为文件指针 m_fp 是共享资源，多个线程同时写入可能会导致数据损坏或日志信息错乱
                //fputs 函数用于向指定的文件流中写入字符串
                //第一个参数 指向要写入文件的字符串的指针
                //第二个参数 指向 FILE 结构的指针，代表一个打开的文件流，之前必须已经通过 fopen freopen fdopen等函数打开
                //成功返回一个非负数 表示写入成功 失败返回EOF 通常为-1
                //single_log.c_str函数将 std::string 转换为C风格的字符串
                fputs(single_log.c_str(),m_fp);
                m_mutex.unlock();//解锁 允许其他线程访问和修改文件
            }
            return nullptr;
        }
};
//宏定义  针对不同的日志级别快速地写入日志消息 
//每个宏都会检查 m_close_log 变量是否为0（即日志功能开启） 如果日志功能开启，宏会调用 write_log 函数，将日志信息写入，并随即使调用 flush 方法确保日志信息被立刻写入文件，而不是停留在内存缓冲区中
//参数 format,... 是一个格式字符串，指定了后续参数（通过... 即可变参数列表提供）如何格式化成字符串
//##__VA_ARGS__ 是一个GCC预处理器的扩展，用于处理可变参数。这个操作符允许宏在传递给write_log函数时，如果没有额外的参数，就不包括逗号，从而避免编译错误
#define LOG_DEBUG(format,...) if(m_close_log==0){Log::get_instance()->write_log(0,format,##__VA_ARGS__);Log::get_instance()->flush();}
#define LOG_INFO(format,...)  if(m_close_log==0){Log::get_instance()->write_log(1,format,##__VA_ARGS__);Log::get_instance()->flush();}
#define LOG_WARN(format,...) if(m_close_log==0){Log::get_instance()->write_log(2,format,##__VA_ARGS__);Log::get_instance()->flush();}
#define LOG_ERROR(format,...)  if(m_close_log==0){Log::get_instance()->write_log(3,format,##__VA_ARGS__);Log::get_instance()->flush();}
#endif