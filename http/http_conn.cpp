#include"http_conn.h"
#include<mysql/mysql.h>
#include<fstream>

//定义http响应的一些状态信息  用于在服务器遇到特定类型的请求错误或问题时 向客户端提供明确的反馈
//成功响应
const char *ok_200_title="OK";//请求成功被服务器接收 理解 并处理
//客户端错误响应
const char *error_400_title="Bad Request";//请求语法错误 或请求有误
const char *error_400_form="Your request has bad syntax or is inherently impossible to statisfy.\n";//请求错误 提供更具体的错误信息
const char *error_403_title="Forbidden";//服务器理解客户端的请求 但拒绝执行此请求
const char *error_403_form="You do not have permission to get file form this server.\n";//更具体地说明客户端权限不足 被禁止访问服务器
const char *error_404_title="Not Found";//请求的资源未在服务器上被找到
const char *error_404_form="The requested file was not found on this server.\n";//更具体地通知客户端请求的文件在服务器上不存在
//服务器错误响应
const char *error_500_title="Internal Error";//服务器遇到了一个阻止它为请求提供服务的错误
const char *error_500_form="There was an unusual problem serving the request file.\n";//更具体的说明无法完成请求的服务

//互斥锁
locker m_lock;
//存储用户名和密码
map<string,string>users;

//initmysql_result函数 使用MYSQL数据库连接池来检索用户数据，并将用户名和密码存储到一个 map容器中
void http_conn::initmysql_result(connection_pool *connPool){
    //从连接池获取一个连接
    //创建一个MYSQL类型的mysql指针，并使用connectionRAII类管理这个数据库连接 参数connPool 是传入的数据库连接池，用于管理和分配数据库连接
    //通过使用RAII和连接池 可以优化数据库连接的使用，避免连接泄露，并减少数据库连接和断开的频繁操作 提高效率
    MYSQL *mysql=nullptr;
    connectionRAII mysqlcon(&mysql,connPool);

    //执行SQL查询 从user表中检索所有的用户名和密码
    //检索需要在HTTP请求中用于用户认证的数据
    if(mysql_query(mysql,"SELECT username,passwd FROM user")){
        //如果查询失败，使用mysql_error函数获取错误详情，并记录错误日志
        LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
    }

    //检索结果集
    //调用 mysql_store_result 函数来获取查询结果集，这个结构集包含了查询返回的所有行
    //mysql_store_result 返回一个指向结果集的指针，没有找到结果则返回nullptr 用于获取SELECT查询的结果 每行包含一组字段值
    MYSQL_RES *result=mysql_store_result(mysql);
    //处理结果集
    //mysql_num_fields 返回给定结果集中字段（列）的数量   
    //mysql_fetch_fields 返回一个指向 MYSQL_FIELD 结构数组的指针，每个结构描述了一个字段，包括字段名 类型 大小等信息 
    int num_fields=mysql_num_fields(result);//返回结果集中的列数
    MYSQL_FIELD *fields=mysql_fetch_fields(result);//返回所有字段结构的数组

    //提取数据并存入map  将用户名和密码存储到 users 映射表中，在处理HTTP请求时，可以快速查找用户密码进行认证
    //循环遍历结果集中的每一行，每行的第一个元素 row[0]是用户名 第二个元素row[1]是密码
    //mysql_fetch_row 使用mysql_store_result检索到的结果集 从中逐行获取数据
    while(MYSQL_ROW row=mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1]=temp2;
    }
}

//文件描述符设置非阻塞模式   参数 fd 文件描述符，通常指的是套接字描述符
int setnonblocking(int fd){
    //获取当前文件描述符的标志
    int old_option=fcntl(fd,F_GETFL);//使用fcntl函数 和 F_GETFL命令获取 fd的当前状态标志
    //修改标志以添加非阻塞选项
    int new_option=old_option|O_NONBLOCK;//将当前的状态标志与 O_NONBLOCK做逻辑或操作  这样设置之后，fd上的I/O操作将不会导致调用者阻塞 如果操作不能立刻完成，调用立刻返回一个错误
    //设置新的标志到文件描述符
    fcntl(fd,F_SETFL,new_option);//将新的标志位设置到fd上
    return old_option;//允许调用者在必要时恢复 fd 的原始状态
}

//addfd函数  用于在epoll事件监听系统中注册文件描述符fd  
//配置了文件描述符的监听事件 并设定其行为
//参数 epollfd:epoll实例的文件描述符   fd:需要注册的文件描述符，通常是一个网络套接字  
//one_shot:指定是否启用 EPOLLONESHOT TRIGMode:指定触发模式  1代表(ET)边缘触发  否则(LT)水平触发
void addfd(int epollfd,int fd,bool one_shot,int TRIGMode){
    //初始化 epoll_event结构
    epoll_event event;
    event.data.fd=fd;//设置 event.data.fd为需要监听的文件描述符 fd

    //设置事件类型
    if(TRIGMode==1){
        //EPOLLIN:表示对应的文件描述符可以读取(非高水位条件)
        //EPOLLET:设置epoll为边缘触发模式，事件只会在状态变化时通知一次
        //EPOLLRDHUP:对端套接字被关闭 或关闭了写操作 这对于探测网络连接是否断开非常有用
        event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    }else{
        event.events=EPOLLIN|EPOLLRDHUP;
    }

    //可选的 EPOLLONESHOT
    if(one_shot){
        //EPOLLONESHOT:确保一个socket连接在任意时刻都只被一个线程处理
        event.events|=EPOLLONESHOT;
    }
 
    //向epoll实例中注册事件  调用epoll_ctl 使用EPOLL_CTL_ADD操作向epollfd指示的epoll实例中注册event中描述的事件
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //设置非阻塞模式
    setnonblocking(fd);//确保fd是非阻塞的  即使在数据未准备好的情况下尝试读写也不会导致线程或进程挂起
}

//removefd函数 用来从epoll实例中移除文件描述符fd 并关闭该文件描述符的功能实现
//参数 epollfd:epoll实例的文件描述符  fd：需要被移除和关闭的文件描述符
void removefd(int epollfd,int fd){
    //从epoll实例中移除描述符
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);//参数0或nullptr 在删除操作中，内核不需要这个数据结构
    //关闭文件描述符  关闭操作会释放fd所占用的资源，包括任何关联的套接字或文件 
    close(fd);
}

//modfd函数 修改epoll实例中已注册文件描述符fd的事件设置 
//参数 epollfd:epoll实例的文件描述符    fd:需要修改事件的文件描述符
//ev：新的事件类型 例如'EPOLLIN' 'EPOLLOUT'等  TRIGMode:指定触发模式 1(ET)边缘触发  其余(LT)水平触发
void modfd(int epollfd,int fd,int ev,int TRIGMode){
    //初始化epoll_event结构
    epoll_event event;
    event.data.fd=fd;

    //设置事件类型
    if(TRIGMode==1){
        event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    }else{
        event.events=ev|EPOLLONESHOT|EPOLLRDHUP;
    }

    //修改文件描述符的事件
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//http_conn类静态成员变量初始化
int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;//文件描述符fd是非负数，-1用作初始化，表示该fd尚未被分配或指定

//关闭连接  
void http_conn::close_conn(bool real_close){
    //条件检查   real_close是否真正关闭连接  +   m_sockfd该连接的套接字文件描述符是否有效 确保不尝试关闭一个已经关闭的或未正确初始化的文件描述符
    if(real_close&&(m_sockfd!=-1)){
        //打印关闭信息
        printf("close %d\n",m_sockfd);
        //从epoll实例中移除文件描述符
        removefd(m_epollfd,m_sockfd);
        //重置文件描述符 并减少用户数
        m_sockfd=-1;
        m_user_count--;
    }
}

//init初始化HTTP连接的多个关键参数 并将该连接添加到epoll事件监听中，准备接收和处理数据
//参数 sockfd:与客户端通信的套接字文件描述符   addr:客户端的地址信息   root:服务器的根目录路径  
//TRIGMode:触发模式   close_log:是否关闭日志    user,passwd,sqlname:用于数据库操作的用户名 密码 和数据库名称
void http_conn::init(int sockfd,const sockaddr_in &addr,char *root,int TRIGMode, int close_log, string user, string passwd, string sqlname){
    ////初始化和配置
    //将传入的套接字描述符和客户端地址存储到类成员变量中
    m_sockfd=sockfd;
    m_address=addr;
    //调用addfd函数将套接字sockfd添加到epoll实例m_epollfd中，使用EPOLLONESHOT确保事件在被处理后必须显示重置
    addfd(m_epollfd,sockfd,true,m_TRIGMode);
    //增加活跃用户计数
    m_user_count++;
    //设置文档根目录，触发模式和日志记录配置
    doc_root=root;
    m_TRIGMode=TRIGMode;
    m_close_log=close_log;
    
    ////设置数据库连接信息
    strcpy(sql_user,user.c_str());
    strcpy(sql_passwd,passwd.c_str());
    strcpy(sql_name,sqlname.c_str());

    //调用内部init方法来进一步的初始化  包括清理旧状态 设置默认值等
    init();
}

//init内部初始化 确保http_conn实例在处理新的连接请求时不会受到旧连接状态的影响  彻底重置所有相关成员变量和缓冲区 
void http_conn::init(){
    ////功能和成员变量重置
    //数据库连接重置
    mysql=nullptr;//清空可能存在的数据库连接指针，准备新的数据库操作
    //发送和接收字节计数器
    bytes_to_send=0;
    bytes_have_send=0;
    //HTTP解析状态
    m_check_state=CHECK_STATE_REQUESTLINE;//设置初始解析状态为请求行解析 这是接收新请求的第一步
    //持久化连接
    m_linger=false;//设置连接不持久，即非 Keep-Alive模式
    //请求方法
    m_method=GET;//默认请求方法为GET
    //初始化 URL 版本 主机名等
    m_url=0;
    m_version=0;
    m_host=0;
    //初始化请求内容长度
    m_content_length=0;
    //重置读写缓冲区索引和状态
    m_start_line=0;
    m_checked_idx=0;
    m_read_idx=0;
    m_write_idx=0;
    //CGI相关标志
    cgi=0;//默认不使用CGI
    //初始化状态 用于指示当前是处理读操作还是写操作
    m_state=0;
    //计数器标志
    timer_flag=0;
    improv=0;
    //缓冲区初始化  清空读写缓冲区和存储实际请求文件名的缓冲区
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}

//从状态机 从读缓冲区中解析出一行内容 返回值类型为行的读取状态 
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    //循环遍历缓冲区
    //从m_checked_idx 开始到 m_read_idx结束   m_checked_idx是检查到的位置索引  m_read_idx是缓冲区中数据的结束位置
    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        temp=m_read_buf[m_checked_idx];
        //HTTP协议对行结束有定义 行结束标志为 '\r\n' 即回车符+换行符
        //如果当前字符是回车符
        if(temp=='\r'){
            //检查下一个字符  1当前字符的位置就是m_read_buf中最后一位(读缓冲区中字符大小为m_read_idx)  2不是读缓冲区最后一位，则看下一位是否是换行符'\n' 
            if((m_checked_idx+1)==m_read_idx){
                return LINE_OPEN;
            }else if((m_read_buf[m_checked_idx+1]=='\n')){
                //找到了 '\r\n' 说明行读取成功 并将 \r和\n 分别用 '\0'替换   同时将 m_checked_idx索引向前移动
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            //在\r 后面没有紧接着\n 说明行有错
            return LINE_BAD;
        }else if(temp=='\n'){
            //判断前一位是否为 \r
            if(m_checked_idx>1&&m_read_buf[m_checked_idx-1]=='\r'){
                //找到 \r\n  同时将 m_checked_idx索引向前移动
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //都没有找到\r 或 \n 说明还要继续找
    return LINE_OPEN;
}

//read_once函数 用于非阻塞模式下从客户端循环读取数据（从套接字读取数据至内部缓冲区），直到没有更多数据可读或连接被关闭
//处理两种不同的触发模式 水平触发(LT) 边缘触发(ET)
bool http_conn::read_once(){
    //检查缓冲区空间 确保读缓冲区有足够的空间来存储新数据
    //如果读索引m_read_idx已经等于或超过缓冲区大小 READ_BUFFER_SIZE ,则没有空间进一步读取，返回false
    if(m_read_idx>=READ_BUFFER_SIZE){
        return false;
    }
    
    int bytes_read=0;//存储每次通过recv调用返回的实际读取的字节数

    //水平触发LT模式  每次触发读事件时调用 recv 尝试读取数据。读取结果立刻增加到 m_read_idx 上
    //recv 尝试从套接字 m_sockfd 读取最多READ_BUFFER_SIZE-m_read_idx字节的数据到缓冲区m_read_buf中 且从m_read_idx出开始
    //recv返回实际读取到的字节数，这些字节数已经被存储到缓冲区中  返回0：表示对端已经关闭了连接，没有更多数据可以读取  返回-1：表示读取操作失败
    if(m_TRIGMode==0){
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        m_read_idx+=bytes_read;//更新m_read_idx索引
        if(bytes_read<=0){
            return false;
        }
        return true;
    }else{
        //ET模式 
        //循环读取数据  一旦事件被触发，必须读取所有可用的数据，因为后续不会再有通知，除非有新数据到来
        while(true){
            bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
            //错误和数据读取结束处理
            if(bytes_read==-1){
                ////读取错误 需要检查errno 
                if(errno==EAGAIN||errno==EWOULDBLOCK)break;//EAGAIN或EWOULDBLOCK：表示非阻塞套接字没有更多数据可读 在ET模式下，这是退出循环的信号，因为所有数据已被读取
                return false;//其他错误
            }else if(bytes_read==0){
                //连接的对方已经正常关闭了连接
                return false;
            }
            m_read_idx+=bytes_read;//成功读取了bytes_read字节数的数据，更新m_read_idx以反映新数据的结束位置
        }
        return true;//成功读取数据到读缓冲区
    }
}

//parse_request_line 解析HTTP请求的起始行 并确定请求的类型 目标URL 和HTTP版本   这是HTTP请求处理的第一步
//http请求行标准格式---------------------------------------------->>>>>>>>>>>>>>>       请求方法->空格->URL->空格->版本->回车符->换行符
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    //解析请求方法
    m_url=strpbrk(text," \t");//strpbrk在text中查找第一个空格或制表符，这通常用来分隔HTTP方法和URL  
    if(!m_url){
        return BAD_REQUEST;//未找到意味着请求格式错误
    }
    *m_url++='\0';//将找到的空格替换为字符串结束符'\0'，从而将方法字符串与后续字符串分开
    char *method=text;//method指向请求方法

    //确定HTTP方法  支持GET 或 POST方法 不区分大小写
    if(strcasecmp(method,"GET")==0){
        m_method=GET;
    }else if(strcasecmp(method,"POST")==0){
        m_method=POST;
        cgi=1;//表示可能需要处理动态资源
    }else{
        return BAD_REQUEST;
    }

    //解析URL和版本
    m_url+=strspn(m_url," \t");//让m_url指向了URL开头位置
    m_version=strpbrk(m_url," \t");//m_version指向了空格或\t，后面紧接着就是version(可能还有空格或\t)
    if(!m_version)return BAD_REQUEST;
    *m_version++='\0';//将找到的空格替换为字符串结束符'\0'，从而将URL与version断开
    m_version+=strspn(m_version," \t");//m_version真正指向了version开头

    //验证HTTP版本
    if(strcasecmp(m_version,"HTTP/1.1")!=0)return BAD_REQUEST;//只支持HTTP/1.1版本

    //处理URL   如果url是以 'http://' 或'https://'开头，跳过这些协议标记，并将URL调整为从第一个'/'开始的路径部分 
    if(strcasecmp(m_url,"http://",7)==0){
        m_url+=7;
        m_url=strstr(m_url,'/');//确保调整后的url是以'/'开头，否则返回错误
    }
    if(strcasecmp(m_url,"https://",8)==0){
        m_url+=8;
        m_url=strstr(m_url,'/');
    }
    if(!m_url||m_url[0]!='/'){
        return BAD_REQUEST;
    }

    //默认页面设置
    if(strlen(m_url)==1)strcat(m_url,"judge.html");//如果URL只是'/',则添加默认页面"judge.html"

    //更新状态并返回
    m_check_state=CHECK_STATE_HEADER;//请求行解析完毕，更新状态为CHECK_STATE_HEADER 准备解析头部
    return NO_REQUEST;//返回 NO_REQUEST表示请求还未处理完，需要继续解析头部
}