#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<stdlib.h>
#include<list>
#include<string.h>
#include<pthread.h>
#include<iostream>
#include"sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool(){
    m_CurConn=0;
    m_FreeConn=0;
}

connection_pool *connection_pool::GetInstance(){
    static connection_pool connPool;//定义了一个connection_pool的静态局部变量
    return &connPool;
}

//构造初始化
void connection_pool::init(string url,string User,string PassWord,string DBName,int Port,int MaxConn,int close_log){
    m_url=url;
    m_User=User;
    m_PassWord=PassWord;
    m_Port=Port;
    m_colse_log=close_log;
    m_DatabaseName=DBName;

//创建数据库连接池
    for(int i=0;i<MaxConn;i++){
        MYSQL *con=nullptr;//创建一个MYSQK类型的指针con，并初始化为nullptr
        con=mysql_init(con);//调用mysql_init(con)函数初始化MySQL连接对象，得到一个MYSQL类型的对象，让con用于连接数据库服务器

        //检查初始化是否成功
        if(con==nullptr){
            LOG_ERROR("MySQL Error");//记录错误日志
            exit(1);//退出程序
        }

        //建立实际的数据库连接
        //调用mysql_real_connect函数建立与MySQL服务器的时间连接。参数需要服务器的URL,用户名，密码，数据库名，端口号等
        //在成功调用mysql_real_connect时返回一个MYSQL对象的指针，失败返回NULL
        con=mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,nullptr,0);

    }
}