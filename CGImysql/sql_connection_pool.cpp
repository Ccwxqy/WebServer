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
    m_close_log=close_log;
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

        //检查是否连接成功
        if(con==nullptr){
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        //将连接加入线程池
        connList.push_back(con);
        //可用空闲连接加一
        ++m_FreeConn;
    }
    //信号量的使用，初始化一个名为reserve的信号量对象，初始值为m_FreeConn
    //任何试图从连接池获取连接的操作都必须首先从这个信号量获取许可
    //确保在没有可用连接时，相关操作能够正确的等待，直到有连接释放回连接池
    reserve=sem(m_FreeConn);
    //设置最大连接数
    m_MaxConn=m_FreeConn;

}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection(){
    MYSQL *con=nullptr;//先创建一个MYSQL类型的指针con，初始化为nullptr
    
    //检查连接池是否为空
    if(connList.size()==0){
        return nullptr;
    }
    //等待可用连接
    reserve.wait();//调用信号量reserve的wait方法请求一个连接 如果信号量计数器为0(即没有可用连接)，改线程将被阻塞，直到信号量的计数值大于0(一般发生在其他线程释放了一个连接时)
    //锁定连接池访问
    //在访问共享资源connList前，通过调用互斥锁lock的lock方法确保当前线程独占访问，确保多线程环境下数据竞争安全
    lock.lock();

    //从连接池中取出一个连接
    con=connList.front();
    connList.pop_front();
    //更新连接的计数
    --m_FreeConn;
    ++m_CurConn;

    //解锁互斥锁，允许其他线程访问连接池
    lock.unlock();
    //返回连接
    return con;

}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con){
    //连接是否有效
    if(con==nullptr){
        return false;
    }
    //加互斥锁，确保多线程安全
    lock.lock();

    //把要释放的连接加入线程池，留作后用
    //同时更新当前线程池的连接数量和可用连接数量
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    //解锁互斥锁
    lock.unlock();

    //连接被释放会连接池，通过reserve调用post方法将信号量加一，与wait方法相反。并通知其他正在因为wait阻塞等待的线程，已经有一个可用线程加入到线程池中
    //标志着资源的释放
    reserve.post();
    return true;
}

//销毁数据库连接池
void connection_pool::Destroypool(){
    //加锁，确保销毁连接池的过程中，没有其他线程可以访问或修改connList
    lock.lock();
    //检查连接列表
    if(connList.size()>0){//如果conList为空表明不需要销毁
        //遍历并关闭所有连接
        //使用迭代器遍历连接列表中的每一个MYSQL指针
        list<MYSQL *>::iterator it;
        for(it=connList.begin();it!=connList.end();++it){
            MYSQL *con=*it;
            mysql_close(con);//调用mysql_close关闭指定的数据库连接，它可以释放服务器资源，避免内存泄漏和其他资源占用问题
        }
        //重置连接计数器并清空连接列表
        m_CurConn=0;
        m_FreeConn=0;
        connList.clear();//清除列表中的所有元素，确保连接池不再持有任何MYSQL对象的指针，防止野指针或无效访问

        //解锁
        lock.unlock();

    }

}

//当前空闲的连接数
int connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

//connection-pool析构函数
connection_pool::~connection_pool(){
    //销毁数据库连接池
    Destroypool();
}


//RAII可以通过构造函数获取资源，并在析构函数中释放资源，RAII确保了资源使用的安全性和正确性
//即使在发送异常或提前返回的情况下，也能保证被适时的释放





//资源管理类connectionRAII,用于自动管理数据库连接的获取和释放，以确保数据库连接能够正确和安全地被回收
//参数：MYSQL **SQL双重指针，用于接收从连接池中取出的数据库连接
//connection_pool *connPool 指向连接池的指针，用于从中获取和释放连接
connectionRAII::connectionRAII(MYSQL **SQL,connection_pool*connPool){
    *SQL=connPool->GetConnection();//从连接池中获取一个数据库连接，并将其地址给 *SQL，使得调用者可以通过SQL指针直接使用数据库连接

    conRAII=*SQL;//将获取的连接(*SQL)存储在类成员conRAII中，以便析构时可以释放；
    poolRAII=connPool;//将连接池对象的指针(connPool)存储在类成员poolRAII中，用于析构时释放连接

}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);//将之前获取的连接conRAII释放回连接池
}