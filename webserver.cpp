#include "webserver.h"

//webserver 构造函数 初始化客户端连接处理 服务器资源路径配置以及定时器管理
webserver::webserver(){
    //初始化 http_conn类对象数组 为http_conn对象数组users分配内存，其中MAX_FD代表服务器可以处理的最大连接数(文件描述符数量) 每个http_conn实例用于处理一个客户端连接，管理HTTP请求和响应
    users=new http_conn[MAX_FD];

    //设置服务器的根目录路径
    char server_path[200];
    getcwd(server_path,200);//获取当前工作目录
    char root[6]="/root";
    m_root=(char *)malloc(strlen(server_path)+strlen(root)+1);//分配内存
    strcpy(m_root,server_path);//复制当前目录到m_root
    strcat(m_root,root);//追加"/root"到路径末尾

    //初始化客户端数据与定时器数组
    users_timer=new client_data[MAX_FD];//这些定时器用于处理如连接超时等事件 
}

//webserver 析构函数 清理在构造函数和类的生命周期中分配的资源
webserver::~webserver(){
    //关闭文件描述符 
    close(m_epollfd);//m_epollfd：用于epoll的文件描述符，负责监控所有注册的事件
    close(m_listenfd);//m_listenfd:服务器监听的文件描述符，用于接收新的客户端连接
    close(m_pipefd[1]);//管道文件描述符 用于进程间通信 
    close(m_pipefd[0]);
    //释放动态分配的内存
    delete[] users;
    delete[] users_timer;
    //删除线程池
    delete m_pool;
}

//init 初始化服务器实例的配置函数
void webserver::init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_mode){
    m_port=port;
    m_user=user;
    m_passWord=passWord;
    m_databaseName=databaseName;
    m_sql_num=sql_num;
    m_thread_num=thread_num;
    m_log_write=log_write;
    m_OPT_LINGER=opt_linger;
    m_TRIGMode=trigmode;
    m_close_log=close_log;
    m_actormodel=actor_mode;
}

//trig_mode 负责设置服务器中的监听套接字和已连接的套接字的触发模式
void webserver::trig_mode(){
    //LT+LT
    if(m_TRIGMode==0){
        m_LISTENTrigmode=0;//监听套接字使用水平触发
        m_CONNTrigmode=0;//连接套接字也使用水平触发
    }
    //LT+ET
    else if(m_TRIGMode==1){
        m_LISTENTrigmode=0;
        m_CONNTrigmode=1;
    }
    //ET+LT
    else if(m_TRIGMode==2){
        m_LISTENTrigmode=1;
        m_CONNTrigmode=0;
    }
    //ET+ET
    else if(m_TRIGMode==3){
        m_LISTENTrigmode=1;
        m_CONNTrigmode=1;
    }
}

//log_write 负责初始化日志系统
void webserver::log_write(){
    //日志是否关闭   m_close_log=0 表示日志是开启的
    if(m_close_log==0){
        //日志写入模式  异步/同步
        if(m_log_write==1)Log::get_instance()->init("./ServerLog",m_close_log,2000,800000,800);
        else Log::get_instance()->init("./ServerLog",m_close_log,2000,800000,0);
    }
}

//sql_pool 初始化和配置数据库连接池 以及初始化用户数据
void webserver::sql_pool(){
    //初始化数据库连接池   调用connection_pool类的单例方法GetConnection来获取数据库连接池的唯一实例  单例模式确保整个应用程序中只有一个数据库连接池实例，有助于统一管理数据库连接
    m_connPool=connection_pool::GetConnection();
    m_connPool->init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);
    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

//thread_pool 初始化服务器中的线程池 这个线程池专门用于管理和分配http_conn类型的工作任务给不同的线程
void webserver::thread_pool(){
    //线程池初始化
    m_pool=new threadpool<http_conn>(m_actormodel,m_connPool,m_thread_num);
}

//eventListen Web服务器启动监听和事件处理准备的各个步骤
void webserver::eventListen(){
    //创建监听套接字
    m_listenfd=socket(PF_INET,SOCK_STREAM,0);//socket函数创建一个IPv4 TCP套接字
    assert(m_listenfd>=0);//确保套接字创建成功，否则终止程序

    //设置套接字选项：优雅关闭连接(SO_LINGER)
    if(m_OPT_LINGER==0){
        struct linger tmp={0,1};//{0，1}表示立刻返回关闭，丢弃任何未发送的数据
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }else if(m_OPT_LINGER==1){
        struct linger tmp={1,1};//{1，1}表示延迟关闭，等待数据发送完成或超时
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    ////绑定地址和监听
    //初始化地址结构体 sockaddr_in
    int ret=0;
    struct sockaddr_in address;//sockaddr_in 一个用于存储IPv4地址信息的结构体
    bzero(&address,sizeof(address));//将address结构体的内存清零，确保结构体中没有随机数据
    address.sin_family=AF_INET;//指定地址族为IPv4
    address.sin_addr.s_addr=htonl(INADDR_ANY);//htonl(INADDR_ANY)：将主机字节顺序的地址转换为网络字节顺序，并用INADDR_ANY，表示服务器将监听所有可用的网络接口(例如本地的所有IP地址)
    address.sin_port=htons(m_port);//将主机字节顺序的端口号转换为网络字节顺序。m_port是服务器将要监听的端口
    //设置套接字选项
    int flag=1;//表示激活SO_REUSEADDR选项
    setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));//设置套接字选项，使得端口可被重用
    //绑定套接字
    ret =bind(m_listenfd,(struct sockaddr *)&address,sizeof(address));//将之前设置的IP地址和端口号与m_listenfd套接字关联起来。准备监听套接字，接收来自指定端口的连接
    assert(ret >=0);//确保bind成功
    //监听套接字
    ret =listen(m_listenfd,5);//使m_listenfd成为一个监听套接字，准备接收连接请求
    assert(ret>=0);//再次确认监听是否成功设置

    //初始化工具类并设置定时器
    utils.init(TIMESLOT);/

    //创建和设置epoll事件监听
    m_epollfd=epoll_create(5);
    assert(m_epollfd!=-1);
    utils.addfd(m_epollfd,m_listenfd,false,m_LISTENTrigmode);
    http_conn::m_epollfd=m_epollfd;//将epoll实例的文件描述符设置为http_conn类的静态成员变量 这允许所有的http_conn实例都能使用同一个epoll文件描述符来监视和管理其套接字事件

    ////设置信号处理和通信管道
    //设置双向通信管道
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);//socketpair函数用于创建一对相互连接的套接字(双向通信管道)，用于进程或线程间的通信  PF_UNIX:表面使用本地套接字 SOCK_STREAM:表面使用面向连接的流套接字
    assert(ret!=-1);//确保socketpair调用成功
    //设置非阻塞模式
    utils.setnonblocking(m_pipefd[1]);
    //注册管道到epoll
    utils.addfd(m_epollfd,m_pipefd[0],false,0);//将管道的读端添加到epoll的监听中
    //信号处理设置
    utils.addsig(SIGPIPE,SIG_IGN);//忽略SIGPIPE信号，通常在发送数据到已关闭的套接字时产生，忽略它可以防止程序意外退出
    utils.addsig(SIGALRM,utils.sig_handler,false);//为定时信号 终止信号 设置处理函数utils.sig_handler 这些信号处理使得服务器可以响应如定时任务和优雅终止等事件
    utils.addsig(SIGTERM,utils.sig_handler,false);//终止信号 
    //设置定时器
    alarm(TIMESLOT);//设置一个定时器，当计时器到达TIMESLOT 单位为秒 后，将发送SIGALRM信号给当前进程

    //全局访问设置
    Utils::u_epollfd=m_epollfd;
    Utils::u_pipefd=m_pipefd;
}

//timer 初始化客户端连接和相关的定时器  1：首先初始化http_conn对象以处理HTTP连接  2：设置连接的定时器，以便管理超时和保持连接的健康
void webserver::timer(int connfd, struct sockaddr_in client_address){
    //初始化http_conn对象
    users[connfd].init(connfd,client_address,m_root,m_CONNTrigmode,m_close_log,m_user,m_passWord,m_databaseName);
    //初始化client_data数据和创建定时器
    users_timer[connfd].address=client_address;
    users_timer[connfd].sockfd=connfd;  //将客户端的地址和套接字文件描述符存储在client_data结构中
    util_timer *timer=new util_timer;//创建新的定时器
    timer->user_data=&users_timer[connfd];
    timer->cb_func=cb_func;
    time_t cur=time(nullptr);//获取当前时间
    timer->expire=cur + 3 * TIMESLOT;//设置定时器的超时时间
    users_timer[connfd].timer=timer;
    utils.m_timer_lst.add_timer(timer);//将新创建的定时器添加到定时器链表中，以便管理和触发
}

//adjust_timer 调整已存在的定时器的超时时间并更新其在链表中的位置 用于响应数据传输事件，以确保连接在活跃时不会因超时而关闭
void webserver::adjust_timer(util_timer *timer){
    //更新定时器的超时时间
    time_t cur=time(nullptr);
    timer->expire=cur + 3 * TIMESLOT;//设置定时器的新超时时间 从而允许连接在更长时间内保持活跃状态
    //调整定时器在链表中的位置
    utils.m_timer_lst.add_timer(timer);
    //记录日志
    LOG_INFO("%s","adjust timer once");
}

//deal_timer 用于处理和管理定时器超时后的动作  主要是执行定时器的回调函数来处理超时事件，然后从定时器链表中移除定时器，并记录相关日志
void webserver::deal_timer(util_timer *timer,int sockfd){
    //执行定时器的回调函数
    timer->cb_func(&users_timer[sockfd]);
    //从定时器链表中删除定时器
    if(timer){
        utils.m_timer_lst.del_timer(timer);
    }
    //记录日志
    LOG_INFO("close fd &d",users_timer[sockfd].sockfd);
}

//dealclientdata 处理新的客户端连接请求  根据m_LISTENTrigmode(监听套接字的触发模式)不同，采取不同的处理方式
bool webserver::dealclientdata(){
    //定义和初始化变量
    struct sockaddr_in client_address;//定义一个sockaddr_in 结构体来存储客户端的地址信息
    socklen_t client_addrlength=sizeof(client_address);//初始化地址长度变量
    //处理连接请求  m_LISTENTrigmode=0 水平触发模式
    if(m_LISTENTrigmode==0){
        int connfd=accept(m_listenfd,(struct sockaddr *)&client_address,&client_addrlength);//使用accept一次性接收一个连接 返回小于0，表示接收连接失败
        if(connfd<0){
            LOG_ERROR("%s:errno is:%d","accept error",errno);
            return false;
        }
        //检查当前用户数是否已经达到最大文件描述符限制MAX_FD，如果是，向客户端显示错误信息并返回false
        if(http_conn::m_user_count>=MAX_FD){
            utils.show_error(connfd,"Internal server busy");
            LOG_ERROR("%s","Internal server busy");
            return false;
        }
        //连接成功 设置定定时器
        timer(connfd,client_address);
    }
    //边缘触发  在边缘触发模式下，可能有多个连接同时到来，因此使用while循环来不断尝试新连接，知道accept调用失败(通常是因为没有更多的待处理连接，即EAGAIN或EWOULDBLOCK错误)
    else{
        while (1)
        {
            int connfd=accept(m_listenfd,(struct sockaddr *)&client_address,&client_addrlength);
            if(connfd<0){
                LOG_ERROR("%s:errno is:%d","accept error",errno);
                break;
            }
            if(http_conn::m_user_count>=MAX_FD){
                utils.show_error(connfd,"Internal server busy");
                LOG_ERROR("%s","Internal server busy");
                break;
            }
            timer(connfd,client_address);
        }
        return false;
    }
    return true;
}

//dealwithsignal 处理通过管道接收到的信号  信号是由另一个线程或信号处理函数发送到 m_pipefd[0](管道的读端),然后在这个函数中被接收和处理
bool webserver::dealwithsignal(bool &timeout,bool &stop_server){
    int ret=0
    int sig;
    char signals[1024];
    //接收信号  使用recv函数从管道的读端m_pipefd[0]读取信号数据。信号数据被存储在signals数组中，这个数组可以存储多个信号值  ret表示接收到的字节个数 每个信号标识符通常是一个字节 
    //recv返回 -1 表示读取过程中出现错误  返回0 表示连接已经关闭
    ret =recv(m_pipefd[0],signals,sizeof(signals),0);
    if(ret==-1){
        return false;
    }else if(ret==0){
        return false;
    }else{
        //处理接收到的信号
        for(int i=0;i<ret;++i){
            switch(signals[i]){
                //SIGALRM 触发定时任务
                case SIGALRM:{
                    timeout=true;
                    break;
                }
                //SIGTERM 平滑停止服务器
                case SIGTERM:{
                    stop_server=true;
                    break;
                }
            }
        }
    }
    return true;
}

//dealwithread 处理读事件 根据服务器的并发模型(Reactor/Proactor)来执行相应的操作
// Reactor模型：在读取数据后，主线程只负责将请求分发给工作线程       Proactor模型：在主线程中完成数据读取和请求准备的全部工作
void webserver::dealwithread(int sockfd){
    //通用操作  获取与指定套接字关联的定时器
    util_timer *timer=users_timer[sockfd].timer;

    //Reactor模型  m_actormodel=1 服务器使用Reactor模型   
    if(m_actormodel==1){
        //检查并调整定时器  如果存在，更新定时器的超时时间，反应该连接仍然活跃，防止在处理请求时连接被错误地关闭
        if(timer){
            adjust_timer(timer);
        }  
        //将读事件放入请求队列
        m_pool->append(users+sockfd,0);//将指定的连接users[sockfd]加入到线程池m_pool的任务队列中
        //处理改进标记和定时器标志
        while(true){
            //循环检查improv标志 这个标志指示是否有改进(如数据处理完毕或状态更新)
            if(users[sockfd].improv==1){
                //http_conn对象是否设置了time_flag标志
                if(users[sockfd].timer_flag==1){
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag=0;
                }
                //清除improv标志并退出循环
                users[sockfd].improv=0;
                break;
            }
        }
    }else{
        //Proactor  主线程负责完成I/O操作(如数据读取)，并将数据处理的任务(如解析和相应构建)委托给工作线程
        //尝试读取数据
        if(users[sockfd].read_once()){
            //如果成功读取到数据，记录日志，显示客户端的IP地址
            LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
            //将读事件放入请求队列
            m_pool->append_p(users+sockfd);//涉及请求的进一步解析和处理，如HTTP请求解析 数据库查询 响应生成等
            //调整连接的定时器
            if(timer){
                adjust_timer(timer);//反应该连接仍然活跃
            }
        }else{
            //主线程读取失败
            deal_timer(timer,sockfd);//处理定时器(可能涉及到关闭连接等操作)
        }
    }
}

//dealwithwrite 负责向客户端发送数据 并根据操作的结构更新定时器或进行必要的错误处理
void webserver::dealwithwrite(int sockfd){
    //通用操作
    util_timer *timer=users_timer[sockfd].timer;
    //Reactor模型  主线程不直接执行写操作 而是将其委托给工作线程处理
    if(m_actormodel==1){
        if(timer)adjust_timer(timer);
        m_pool->append(users+sockfd,1);
        while (true)
        {
            if(users[sockfd].improv==1){
                if(users[sockfd].timer_flag==1){
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag=0;
                }
                users[sockfd].improv=0;
                break;
            }
        }
    }else{
        //Proactor
        if(users[sockfd].write()){
            LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
            if(timer)adjust_timer(timer);
        }else{
            deal_timer(timer,sockfd);
        }
    }
}

//eventLoop 负责处理所有的网络事件，包括新的客户端连接 数据读写 信号处理以及定时器事件
void webserver::eventLoop(){
    //初始化标志
    bool timeout=false;//用于标记定时任务是否需要执行
    bool stop_server=false;//用于控制服务器是否停止运行的标志
    //主循环  直到stop_server被设置为true
    while(!stop_server){ 
        //等待事件发生   epoll_wait 监听事件 -1表示无超时(永久等待)，直到事件发生   
        int number=epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        //返回错误且错误不是由中断引起(EINTR)，记录错误并退出循环
        if(number<0&&errno!=EINTR){
            LOG_ERROR("%s","epoll failure");
            break;
        }
        //遍历事件  根据文件描述符和事件类型进行处理
        for(int i=0;i<number;i++){
            int sockfd=events[i].data.fd;
            //处理新的客户端连接
            if(sockfd==m_listenfd){
                bool flag=dealclientdata();//接受连接和初始化新的客户端会话
                //处理失败，返回flag=false,跳过当前循环迭代，继续下一个事件
                if(flag==false)continue;
            }
            //处理异常事件
            else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer=users_timer[sockfd].timer;
                deal_timer(timer,sockfd);
            }
            //处理信号    事件发生在管道的读端(m_pipefd[0]用于信号通知)，并且是读事件，处理接收到的信号
            else if((sockfd==m_pipefd[0])&&(events[i].events & EPOLLIN)){
                bool flag=dealwithsignal(timeout,stop_server);
                if(flag==false)LOG_ERROR("%s","dealclientdata failure");
            }
            //处理数据读取事件
            else if(events[i].events*EPOLLIN){
                dealwithread(sockfd);
            }
            //处理数据写入事件
            else if(events[i].events&EPOLLOUT){
                dealwithwrite(sockfd);
            }
        }

        //定期执行任务
        if(timeout){
            utils.timer_handler();
            LOG_INFO("%s","timer tick");
            timeout=false;
        }
    }
}