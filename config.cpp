#include "config.h"

//Config类 构造函数
Config::Config(){
    PORT=9006;
    LOGWrite=0;//默认同步写入
    TRIGMode=0;//触发组合模式  默认listenfd LT +connfd LT
    LISTENTringmode=0;//默认LT
    CONNTrigmode=0;//默认LT
    OPT_LINGER=0;//默认不使用优雅关闭连接
    sql_num=8;//
    thread_num=8;
    close_log=0;//默认日志打开
    actor_model=0;//默认 proactor模型
}

//parse_arg函数  使用标准的getopt函数来解析命令行参数  
void Config::parse_arg(int argc, char* argv[]){
    int opt;//存储当前解析到的字符
    const char *str="p:l:m:o:s:t:c:a:";//定义有效选项和哪些选项需要参数的字符串  例如 'p:' 表示 '-p'是一个有效选项，并且它需要一个参数 如  '-p 8080'
    //循环调用getopt  参数1:来自main函数的参数数量  参数2: 来自main函数的参数字符串数组 参数3: 一个包含有效选项字符的字符串 如果一个选项后面跟有冒号:，表示这个选项需要一个参数
    while((opt=getopt(argc,argv,str))!=-1){
        switch (opt)
        {
        case 'p':{
            PORT=atoi(optarg);//转换命令行字符串为整数值，并赋值给相应的成员变量
            break;
        }
        case 'l':{
            LOGWrite=atoi(optarg);
            break;
        }
        case 'm':{
            TRIGMode=atoi(optarg);
            break;
        }
        case 'o':{
            OPT_LINGER=atoi(optarg);
            break;
        }
        case 's':{
            sql_num=atoi(optarg);
            break;
        }   
        case 't':{
            thread_num=atoi(optarg);
            break;
        }
        case 'c':{
            close_log=atoi(optarg);
            break;
        }
        case 'a':{
            actor_model=atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}