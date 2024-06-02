#include"config.h" //负责解析命令行

int main(int argc,char* argv[]){

    //定义数据库连接信息  初始化数据库连接   服务器启动时会创建多个数据库连接。这些连接通过使用提供的用户名、密码、数据库名来建立，使得服务器能够执行SQL查询和其他数据库操作
    string user="name";
    string passwd="passwd";
    string databasename="Yourdb";

    //解析命令行参数
    Config config;
    config.parse_arg(argc,argv);

    //初始化Webserver实例
    webserver server;
    server.init(config.PORT,user,passwd,databasename,config.LOGWrite,config.OPT_LINGER,config.TRIGMode,config.sql_num,config.thread_num,config.close_log,config.actor_model);

    //启动日志系统
    server.log_write();

    //初始化数据库连接池
    server.sql_pool();

    //创建线程池
    server.thread_pool();

    //设置触发模式
    server.trig_mode();

    //监听网络事件
    server.eventListen();

    //启动事件循环
    server.eventLoop();

    //主函数返回
    return 0;
}