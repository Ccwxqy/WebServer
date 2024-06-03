#include"http_conn.h"
#include<mysql/mysql.h>
#include<fstream>

//定义http响应的一些状态信息  用于在服务器遇到特定类型的请求错误或问题时 向客户端提供明确的反馈
//成功响应
const char *ok_200_title="OK";//请求成功被服务器接收 理解 并处理
//客户端错误响应
const char *error_400_title="Bad Request";//请求语法错误 或请求有误
const char *error_400_form="Your request has bad syntax or is inherently impossible to staisfy.\n";//请求错误 提供更具体的错误信息
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
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;
        m_url=strchr(m_url,'/');//确保调整后的url是以'/'开头，否则返回错误
    }
    if(strncasecmp(m_url,"https://",8)==0){
        m_url+=8;
        m_url=strchr(m_url,'/');
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

//parse_headers 用于解析HTTP请求头字段 
//请求头标准格式 ----------------------------------------->>>>> 头部字段名->:->值->回车符->换行符    -----|
//------------------------------------------------------->>>>> 头部字段名->:->值->回车符->换行符         |---->>>请求头部
//------------------------------------------------------->>>>> 头部字段名->:->值->回车符->换行符   ------|
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    //处理请求头部结束的情况     HTTP协议规定：在请求头结束后会紧接着加入一行空白行，即为一行全为'\0'字符，代表了请求头部的结束
    if(text[0]=='\0'){
        //内容长度检查  如果m_content_length不为零，说明请求有内容体(通常与POST请求相关),那么状态就转移到CHECK_STATE_CONTENT以准备读取请求内容体
        if(m_content_length!=0){
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;//如果没有请求体，请求头已经完全解析完成，返回GET_REQUEST表示GET请求可以被处理   
        //解析具体头部字段
    }else if(strncasecmp(text,"Connection:",11)==0){
        //Connection头处理
        text+=11;
        text+=strspn(text," \t");//text指向了 Connection:之后 值 的开始部分(跳过可能存在的TAB键或空格)
        if(strcasecmp(text,"Keep-alive")==0){
            m_linger=true;//保持连接
        }
    }else if(strncasecmp(text,"Content-length:",15)==0){
        //获取请求的内容长度
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atol(text);//提取并转换Content-length的值，存储在m_content_length中
    }else if(strncasecmp(text,"Host:",5)==0){
        //解析请求头中的 Host
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }else{
        //处理为止头部  对于不识别的头部字段，记录信息，以便进行调试或日志记录
        LOG_INFO("oop!unknow header: %s",text);
    }
    return NO_REQUEST;//头部字段已被处理，但请求还需要进一步读取 请求体或处理其他请求头
}

//parse_content 用于解析HTTP请求体 一般是处理POST请求  关键在于是否已经接收完整的请求内容
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    //检查内容完整性
    //检查已经读取到的数据索引 m_read_idx是否至少为 m_content_length(请求内容的长度)+m_check_idx(已经检查到的位置索引),确保整个请求内容已经被接收到读缓冲区
    if(m_read_idx>=(m_content_length+m_checked_idx)){
        //设置内容体结束标志
        text[m_content_length]='\0';//在内容体的末尾添加字符串结束符'\0'，这样text指向的数据可以作为标准的C字符串处理
        //保存内容体
        m_string=text;
        //如果内容体被成功解析，即使是POST请求，这里仍返回GET_REQUEST；
        return GET_REQUEST;
    }
    //内容没有完整被接收 服务器还需要继续读取数据
    return NO_REQUEST;
}

//process_read 负责处理HTTP请求的读取和解析过程 根据当前的解析状态 m_check_state来调用响应的解析函数，并管理解析过程中的状态转换
//包含从请求行的解析到请求头的处理，再到请求体的读取
http_conn::HTTP_CODE http_conn::process_read(){
    //初始化状态和变量
    LINE_STATUS line_status=LINE_OK;//初始化line_status表示行解析状态
    HTTP_CODE ret=NO_REQUEST;//ret表示尚未得到完整的请求
    char *text=0;//指向当前正在解析的文本行

    //循环解析
    //条件1 m_check_state==CHECK_STATE_CONTENT&&line_status==LINE_OK:表示当前解析状态是内容体解析并且当前行的状态是LINE_OK，继续循环 这意味着内容体可能未完成解析，需要继续处理剩余数据
    //条件2 (line_status==parse_line())==LINE_OK:表示尝试解析新的一行，如果解析成功返回LINE_OK，则继续循环。这里实现一个边读取边赋值的操作，确保每次循环都重新评估当前行的解析状态
    while((m_check_state==CHECK_STATE_CONTENT&&line_status==LINE_OK)||((line_status=parse_line())==LINE_OK)){
        //行数据提取 调用get_line()方法获取当前解析的行，函数返回指向读缓冲区中当前行起始位置的指针，该位置由m_start_line定位
        text=get_line();
        //更新行起始位置  在每次循环开始时更新m_start_line的值为m_checked_idx(标识读缓冲区中已经检查到的位置)。为下一次行解析做准备，确保get_line()能够正确获取到新的行起始位置
        m_start_line=m_checked_idx;
        //日志记录 将当前解析的行内容输出到日志系统 
        LOG_INFO("%s",text);
        //根据当前的解析状态m_check_state决定下一步的操作
        switch(m_check_state){
            //解析请求行
            case CHECK_STATE_REQUESTLINE:{
                ret=parse_request_line(text);//调用parse_request_line解析HTTP请求行 
                if(ret==BAD_REQUEST)return BAD_REQUEST;//说明请求行格式不正确
                break;//状态转换，如果请求行解析成功，m_check_state会在parse_request_line方法中更新为CHECK_STATE_HEADER，以便开始解析头部
            }
            //解析请求头
            case CHECK_STATE_HEADER:{
                ret=parse_headers(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret==GET_REQUEST){
                    return do_request();//如果parse_headers函数返回GET_REQUEST，表示头部已经完全解析且没有请求体的存在，如GET请求，则调用do_request来处理请求并生成响应
                }
                break;//状态转换，如果content-length长度不为零，或请求方法是POST，m_check_state在parse_headers内杯被更新为CHECK_STATE_CONTENT
            }
            //解析请求体
            case CHECK_STATE_CONTENT:{
                ret=parse_content(text);
                if(ret==GET_REQUEST){
                    return do_request();
                }
                line_status=LINE_OPEN;//标识请求体可能还未完全接收，需要继续接收数据
                break;
            }
            //默认错误处理
            default:
                return INTERNAL_ERROR;//内部处理错误
        }
    }
    return NO_REQUEST;//表示当前还没有接收到完整的请求，需要继续从网络读取数据
}

//do_request函数 用于处理解析完成的HTTP请求，根据请求的类型(如GET或POST)和路径来决定服务器如何响应  涉及到请求的最终处理和资源定位
//函数执行任务包括 构造文件路径  处理CGI请求(例如处理登陆和注册) 检查文件状态 并最终准备文件内容以供响应
http_conn::HTTP_CODE http_conn::do_request(){
    //复制文档更目录到文件路径
    strcpy(m_real_file,doc_root);//将服务器的根目录路径复制到m_real_file变量中，作为构建最终文件路径的基础
    //获取根目录的长度
    int len=strlen(doc_root);//用于在根目录路径后面正确地拼接具体地文件路径
    //查找URL中最后一个斜杠
    const char *p=strrchr(m_url,'/');//p指针指向m_url中最后一个斜杠的位置。


    //====================================================      静        态        ===============================================================
    //检查URL中最后一个斜杠后的字符来决定加载哪个静态页面       如果是 '0'  加载到注册页面 '/register.html'
    // '1'  加载到登陆页面 '/log.html'              '5'   加载到图片展示页面 '/picture.html'
    // '6'  加载到视频播放页面 '/video.html'         '7'  加载到粉丝页面 '/fans.html'
    
    if(*(p+1)=='0'){
        char *m_url_real=(char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/register.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }else if(*(p+1)=='1'){
        char *m_url_real=(char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/log.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }else if(*(p+1)=='5'){
        char *m_url_real=(char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/picture.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }else if(*(p+1)=='6'){
        char *m_url_real=(char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/video.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }else if(*(p+1)=='7'){
        char *m_url_real=(char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/fans.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }else{
        strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    }



    //======================================================    动       态        =============================================================
    //处理CGI请求，动态地根据请求类型构造服务端资源路径。通过对URL的解析和路径的动态构建，服务器能够根据用户请求(如登陆或注册)返回相应的动态内容
    //检查cgi变量是否为1，这表明当前请求需要通过CGI脚本来处理
    //*(p+1)检查URL中斜杠后第一个字符,'2'通常代表登陆操作，'3'代表注册操作
    if(cgi==1&&(*(p+1)=='2'||*(p+1)=='3')){
        //判断是登陆还是注册
        char flag=m_url[1];
        //构造真实URL路径
        char*m_url_real=(char *)malloc(sizeof(char) * 200);//分配一块内存在构造实际的文件路径
        strcpy(m_url_real,"/");//令'/'为m_url_real的开头第一个字符
        strcat(m_url_real,m_url+2);//跳过m_url前两个字符，m_url一个字符为'/'，第二个字符为指示操作类型'2'或'3'
        strncpy(m_real_file+len,m_url_real,FILENAME_LEN-len-1);//将新构造的路径复制到m_real_file中，从doc_root的结尾开始拼接，确保整个文件的路径在预定的长度限制之内
        free(m_url_real);//释放内存，避免内存泄漏

        //从CGI请求中提取用户提交的表单数据，特别是用户名和密码
        //提取用户名
        char name[100],password[100];//用于存储解析出的用户名和密码  
        int i;
        //m_string包含从客户端接收到的数据 例如"user=11111&passwd=22222"
        for(int i=5;m_string[i]!='&';++i){//i从5开始，因为"user="占据了前五个字节
            name[i-5]=m_string[i];
        }
        name[i-5]='\0';//在name的末尾加上'\0'，确保它是一个有效的字符串
        //提取密码
        int j=0;
        for(i=i+7;m_string[i]!='\0';++i,++j){//从找到的字符'&'后加7个字符，因为"passwd="有七个字符
            password[j]=m_string[i];
        }
        password[j]='\0';//在password的末尾加上'\0'，确保它是一共有效的字符串

        //服务器端处理注册请求 包括创建SQL查询以将新用户数据插入数据库，同时检查是否存在重名情况，并根据操作结果更新用户界面
        if(*(p+1)=='3'){
            //构建SQL插入语句
            char *sql_insert=(char *)malloc(sizeof(char) * 200);//为sql_insert分配200字节
            strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert,"'");
            strcat(sql_insert,name);
            strcat(sql_insert,"', '");
            strcat(sql_insert,password);
            strcat(sql_insert,"')");//构建了一个完整的SQL插入语句，用于向数据库的用户表('user')中添加新的用户名('username')和密码('passwd')
            
            //检查重名并执行SQL
            if(users.find(name)==users.end()){//检查内存中的用户映射('users')是否已经包含该用户名，如果没有找到，即返回users.end()，表示无重名，可以进行注册
                m_lock.lock();//确保在修改用户映射和执行数据库操作时线程安全
                int res=mysql_query(mysql,sql_insert);//mysql_query执行前面构建的SQL插入语句 操作成功返回0
                users.insert(pair<string,string>(name,password));//把相同的一份name和password也插入服务器内存users中，保持内存数据与数据库MYSQL同步
                m_lock.unlock();

                //成功把注册用户名和密码插入了MYSQL数据库
                if(!res){
                    strcpy(m_url,"/log.html");//跳转到登陆界面
                }else{
                    //插入错误
                    strcpy(m_url,"/registerError.html");
                }
            }else{
                //重名错误
                strcpy(m_url,"/registerError.html");
            }
        }else if(*(p+1)=='2'){//如果是登陆，直接判断。若浏览器端输入的用户名和密码在内存数据库users中能找到，返回1 否则返回0
            if(users.find(name)!=users.end()&&users[name]==password){
                strcpy(m_url,"/welcome.html");
            }else{
                strcpy(m_url,"/logError.html");
            }
        }
    }


    //文件存在性 权限 和类型检查
    if(stat(m_real_file,&m_file_stat)<0)return NO_RESOURCE;//stat函数用于检验m_real_file是否有效，成功则返回0，并将m_real_file文件信息存储在m_file_stat中,失败返回 -1
    if(!(m_file_stat.st_mode&S_IROTH))return FORBIDDEN_REQUEST;//文档没有适当的读权限
    if(S_ISDIR(m_file_stat.st_mode))return BAD_REQUEST;//请求的是一个目录而非文件   

    //打开文件  在确认文件存在且可以访问之后的操作
    int fd=open(m_real_file,O_RDONLY);//open函数用只读模式('O_RDONLY')打开m_real_file指定的文件。 fd是文件描述符，如果文件成功打开，它将是一个非负的，如果打开是否，fd=-1
    //内存映射文件内容   mmap()函数将打开的文件内容映射到调用进程的地址空间。这允许文件内容被当作内存中的数据处理，提高文件操作效率
    //mmap()参数  0：建议内核为映射选择起始地址，内核通常会选择一个合适的地址   m_file_stat.st_size：映射文件的大小   PROT_READ:映射区域的保护方式，设置为可读
    //MAP_PRIVATE:映射区域的写操作不会写回原文件，而是创建一个写时复制的私有副本   fd：文件描述符，映射哪个文件     0：映射从文件的哪个偏移量开始，这里从文件开始从映射
    m_file_address=(char *)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    //关闭文件描述符
    close(fd);//打开的文件描述符在完成映射后应该被关闭，因为映射后对文件的操作不再需要通过文件描述符进行
    //返回状态
    return FILE_REQUEST;//表示文件请求处理成功，文件已经准备好被发送到客户端   意味着HTTP响应可以直接使用映射的数据来填充响应体
}


//unmap()函数  释放之前通过mmap()函数映射的文件内存区域 确保系统资源得到正确的释放，避免内存泄漏
void http_conn::unmap(){
    //检查m_file_address释放存在
    if(m_file_address){
        //释放映射的内存  使用munmap()函数释放内存映射 参数 m_file_address:指向映射区域的指针，即mmap()的返回值  m_file_stat.st_size：映射区域的大小，即文件的大小
        munmap(m_file_address,m_file_stat.st_size);
        //重置m_file_address
        m_file_address=0;
    }
}

//write()函数 使用非阻塞I/O 和边缘触发模式来高效地发送数据，同时处理部分写和EAGAIN错误(表示套接字缓冲区已满)
bool http_conn::write(){
    int temp=0;
    //初始条件检查
    if(bytes_to_send==0){
        //如果没有数据需要发送 bytes_to_send==0 则修改套接字感兴趣地事件为 EPOLLIN(读取事件)，重新初始化连接，准备接收新的数据
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        //重置http_conn对象地状态，准备它来处理下一个新的HTTP请求。在HTTP服务器中，通常同一个连接可以被用来处理多个请求，也就是HTTP/1.1 Keep-alive特性
        //重置连接状态包括  清空读写缓冲区  重置所有状态变量，如解析状态、头部处理状态等  重新初始化成员变量，准备接收和解析新的请求
        init();
        return true;
    }

    //发送数据循环
    while(1){
        //writev()函数 从m_iv指向的多个缓冲区中收集数据并写入到m_sockfd指定的文件(socket)描述符中，m_iv_count指示有多少个缓冲区
        //返回写入的字节数，如果成功，这个数值应该是所有写入缓冲区的数据总和。如果发生错误，返回-1，并设置errno
        temp=writev(m_sockfd,m_iv,m_iv_count);
        //错误处理  表明writev()在尝试写入数据时发生了错误
        if(temp<0){
            //处理EAGAIN错误  当errno设置为EAGAIN，这意味着非阻塞socket的写缓冲区已满，当前没有更多空间可用于写入数据，表示现在不能写，稍后再试试
            if(errno==EAGAIN){
                //修改文件描述符的感兴趣的事件为EPOLLOUT(可写事件)，当socket缓冲区有空间可写时，epoll会再次通知程序
                modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
                return true;//表示这次写操作尽管未完成，但是连续保持有效，服务器应该等待下一个可写事件再继续写入
            }
            //其他写入错误
            unmap();
            return false;
        }

        //更新已发送和待发送的字节计数
        bytes_have_send+=temp;
        bytes_to_send-=temp;

        //调整iovec结构  根据已发送的字节量更新iovec结构，以确保下一次writev()调用正确地发送剩余的数据
        //已发送的字节数超过或等于第一个iovec结构的长度  当bytes_have_send>=m_iv[0].iov_len，说明第一个iovec的数据已经完全发送
        if(bytes_have_send>=m_iv[0].iov_len){
            m_iv[0].iov_len=0;//表面第一个iovec中没有剩余数据要发送
            m_iv[1].iov_base=m_file_address+(bytes_have_send-m_write_idx);//用m_file_address作为基地址加上偏移量(已经发送的总字节数-写缓冲区的起始发送位置)指示在映射文件中的当前位置
            m_iv[1].iov_len=bytes_to_send;
        }else{
            //已发送的字节数小于第一个iovec结构的长度
            m_iv[0].iov_base=m_write_buf+bytes_have_send;
            m_iv[0].iov_len=m_iv[0].iov_len-bytes_have_send;
        }

        //检查是否所有数据都已经发送完毕
        if(bytes_to_send<=0){
            unmap();//释放文件映射资源
            modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);//将感兴趣的事件从可写EPOLLOUT改为可读EPOLLIN  表示服务器端现在准备接收来自客户端的进一步数据或新的请求

            //处理连接持久性  m_linger反映了HTTP连接的持久性设置
            if(m_linger){
                //表示客户端和服务器均希望保持连接开启，以便客户端可以在同一个连接上发送多个请求。此时调用init()重新初始化连接状态，准备接受新的请求
                init();
                return true;
            }else{
                //客户端或服务器端期望关闭连接 返回false，调用方应根据这个返回值来关闭socket连接
                return false;
            }
        }
    }
}


//add_response函数 用于向写缓冲区m_write_buf中添加响应数据 采用可变参数列表来格式化输出
bool http_conn::add_response(const char *format,...){
    //检查缓冲区空间
    if(m_write_idx>=WRITE_BUFFER_SIZE){
        return false;
    }

    //初始化可变参数列表
    va_list arg_list;//定义了一个va_list类型的变量arg_list来存储可变参数
    va_start(arg_list,format);//va_start函数初始化arg_list，使其指向可变参数列表中的第一个参数

    //格式化字符串并写入缓冲区   使用vsnprintf函数将格式化后的字符串写入m_write_buf中当前m_write_idx指向的位置
    //WRITE_BUFFER_SIZE-1-m_write_idx：计算的是缓冲区剩余的空间大小，'-1'保留一个字符的空间用于字符串的终止符'\0'
    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    //如果len大于或等于可用空间，说明写缓冲区空间不足以存储格式化后的字符串
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
        va_end(arg_list);
        return false;
    }

    //更新写索引
    m_write_idx+=len;
    //结束可变参数列表的使用
    va_end(arg_list);
    //记录日志
    LOG_INFO("request:%s",m_write_buf);
    //返回true 表示响应内容成功添加到缓冲区
    return true;
}


//HTTP响应格式                     协议版本  空格   状态码  空格  状态码描述   回车符  换行符 ---------->状态行
//                                 头部字段名   :   值    回车符   换行符 ------------|
//                                 头部字段名   :   值    回车符   换行符 ------------|----->响应头 
//                                 头部字段名   :   值    回车符   换行符 ------------| 
//                                 回车符  换行符    --------------------------------->空行
//                                 XXXXXXXXXXXXXXXXXXXXXXXX-------------------------->响应正文

//构建HTTP响应行
bool http_conn::add_status_line(int status,const char *title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);//示例输出：'HTTP/1.1 200 OK\r\n'
}
//构建HTTP响应头
bool http_conn::add_headers(int content_len){
    //包括内容长度  连接保持状态 和一个空白行，用于分割头部和主题
    return add_content_length(content_len)&&add_linger()&&add_blank_line();
}
//向响应头中加入Content-length 指示响应主体的字节长度
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);//示例输出：'Content-Length:1234\r\n'
}
//向响应头中加入Content-Type头部 设置为 'text/html'
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n","text/html");//示例输出：'Content-Type:text/html'
}
//向响应头中加入Connection头部 根据m_linger的值决定是否保持连接
bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n",(m_linger==true)?"Keep-alive":"close");//示例输出'Connection:Keep-alive'
}
//加空白行
bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}
//将具体的响应主体内容加到缓冲区
bool http_conn::add_content(const char *content){
    return add_response("%s",content);
}

//基于不同的HTTP请求处理结果构建适当的响应
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        //处理内部错误
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))return false;//如果添加内容失败，返回false
            break;
        }
        //处理错误请求
        case BAD_REQUEST:{
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))return false;
            break;
        }
        //处理禁止错误
        case FORBIDDEN_REQUEST:{
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))return false;
            break;
        }
        //处理文件请求
        case FILE_REQUEST:{
            add_status_line(200,ok_200_title);
            //处理非空文件
            if(m_file_stat.st_size!=0){
                //添加头部
                add_headers(m_file_stat.st_size);
                //设置iovec结构
                m_iv[0].iov_base=m_write_buf;//m_iv[0]被指向m_write_buf中累计的响应头部
                m_iv[0].iov_len=m_write_idx;//长度为写缓冲区有多少数据
                m_iv[1].iov_base=m_file_address;//m_iv[1]被指向m_file_address处内存映射的实际文件数据
                m_iv[1].iov_len=m_file_stat.st_size;//长度为文件的整体大小
                m_iv_count=2;//表示有两块数据要发送：头部和文件内容
                bytes_to_send=m_write_idx+m_file_stat.st_size;//计算  头部长度+文件大小
                return true;
            }else{
                //处理空文件
                const char *ok_string="<html><body></body></html>";//服务器生成一个简单的HTML页面作为响应
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))return false;
            }
        }
        //其他情况
        default:
            return false;
    }
    //在处理完特定的响应后，此部分代码将最终准备发送的数据定位在m_write_buf中      设置iovec结构和发送数据
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count=1;
    bytes_to_send=m_write_idx;
    return true;
}

//process()函数  负责整个请求处理的流程，包括读取请求 处理请求并准备响应  以及根据读写结果调整socket的状态  通过epoll事件驱动来高效处理并发的网络连接
void http_conn::process(){
    //读取请求并处理
    HTTP_CODE read_ret=process_read();
    //判断读取结果
    if(read_ret==NO_REQUEST){
        //请求数据不完整或需要更多数据才能完成解析
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);//重新设置socket为EPOLLIN状态，即告诉epoll再次等待该socket的读事件
        return;
    }
    //处理响应写入
    //一但请求被完全读取和解析，process_write根据解析生成HTTP响应   process_write返回true 代表响应已经成功准备(即数据已经正确放入发送缓冲区)
    bool write_ret=process_write(read_ret);
    //处理写入结果
    if(!write_ret){
        close_conn();//响应失败，可能是由于内部错误或资源问题  关闭当前连接，释放相关资源
    }
    //调整socket为可写状态
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);//不管写入成功与否，都将socket设置为EPOLLOUT状态  这表示socket现在应该准备好写入数据到网络中
}