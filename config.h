#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

//存储和管理web服务器的配置参数 提供解析命令行参数功能 并将解析的结果存储在类的成员变量中 使得web服务器可以在启动时通过命令行参数来设定其运行参数，提高了灵活性和可配置性
class Config{
    //成员变量     存储从命令行参数中解析得到的配置信息
    public: 
        int PORT;//服务器监听的端口号
        int LOGWrite;//日志写入模式  同步/异步
        int TRIGMode;//事件触发模式组合 
        int LISTENTringmode;//用于监听套接字的触发模式
        int CONNTrigmode;//用于已连接套接字的触发模式
        int OPT_LINGER;//设置套接字的延迟关闭行为
        int sql_num;//数据库连接池中的连接数量
        int thread_num;//线程池中线程的数量
        int close_log;//是否关闭日志记录功能
        int actor_model;//并发模型的选择  Reactor/Proactor


    //成员函数
    public:
        Config();
        ~Config(){};
        void parse_arg(int argc, char* argv[]);//负责解析命令行参数，将参数值设置到相应的成员变量中
};
#endif