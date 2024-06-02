#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<unistd.h> //提供了对POSIX操作系统API的访问  例如 读写文件 获取进程ID 等
#include<signal.h> //处理不同的信号量，可以捕捉 忽略 或改变信号处理程序
#include<sys/types.h> //定义了数据类型 如用于进程IDs  pid_t  size_t ssize_t 等
#include<sys/epoll.h> //提供了对Linux特有的 epoll 接口的访问 用于 I/O事件的多路复用监控
#include<fcntl.h> //用于文件控制 例如改变已打开的文件的属性
#include<sys/socket.h> //用于网络通信 包括创建套接字 绑定 监听 等操作
#include<netinet/in.h> //提供互联网地址族 用于数据格式转换和地址格式转换
#include<arpa/inet.h> //用于互联网操作  包括地址转换等功能
#include<assert.h> //提供一个诊断功能 用于程序调试
#include<sys/stat.h> //用于获取文件属性
#include<string.h> //提供字符串处理函数
#include<pthread.h> //多线程编程 包括线程的创建  同步 等
#include<stdio.h> //标准输入输出库 提供文件读写函数 如 printf sacnf fopen 等
#include<stdlib.h> //提供 内存分配 程序退出 字符串转换 等功能
#include<sys/mman.h> //提供内存管理功能 包括内存映射
#include<stdarg.h> //用于实现可变参数函数
#include<errno.h> //通过错误号来报告错误条件  是一个全局变量 存储最后一次错误代码
#include<sys/wait.h>//用于处理子进程 如wait用于父进程等待子进程结束
#include<sys/uio.h> //提供了矢量 I/O 操作的定义 ，如 readv函数 writev 函数
#include<map> //提供map容器

#include"../lock/locker.h"
#include"../CGImysql/sql_connection_pool.h"
#include"../timer/lst_timer.h"
#include"../log/log.h"


//处理http请求连接
class http_conn{
    public:
        //静态常量
        static const int FILENAME_LEN=200;//定义了文件名的最大长度为200字节 确保在处理文件路径或文件名时，程序能正确的分配足够的内存空间来存储这些字符串，同时避免缓冲区溢出
        static const int READ_BUFFER_SIZE=2048;//设置 读缓冲区 的大小为2048字节，这个缓冲区用于从客户端接收数据  一般读缓冲区用来临时存储从套接字接收的原始数据
        static const int WRITE_BUFFER_SIZE=1024;//设置 写缓冲区 的大小为1024字节，这个缓冲区用于存储准备发送到客户端的数据 在HTTP响应时，服务器发送的内容数据首先被写入 写缓冲区
        //枚举类型
        //HTTP请求方法
        enum METHOD{
            GET=0,//GET 请求服务器返回指定资源的内容  不会引起服务器上的状态变化
            POST,//用于提交数据到服务器
            HEAD,//类似于GET方法 服务器在响应中只返回头部信息 不返回实际数据
            PUT,//向指定的资源位置上传其最新内容
            DELETE,//请求服务器删除指定的资源
            TRACE,//回显服务器收到的请求，主要用于诊断或测试
            OPTIONS,//查询针对资源或服务器本身支持的方法 可以用来检查服务器功能或服务器支持的安全选项
            CONNECT,//请求代理服务器建立一个到目标资源的隧道 用于SSL加密服务器的连接 HTTPS
            PATCH//用于对资源应用部分修改
        };
        //HTTP请求的不同阶段
        enum CHECK_STATE{
            CHECK_STATE_REQUESTLINE=0,//请求第一阶段 服务器读取并解析HTTP请求的起始行  请求行 包括 1方法(如 GET POST等) 2请求的资源的URL 3HTTP版本
            CHECK_STATE_HEADER,//一旦 请求行 被解析，服务器进入 头部 解析阶段。服务器读取请求的头部字段 包括认证信息 客户端的浏览器类型 支持的响应格式等
            CHECK_STATE_CONTENT//在处理请求行和请求头之后 如果请求方法或头部信息表面存在请求体(例如POST请求中的表单数据),服务器将处理请求的内容部分
        };
        //服务器在处理HTTP请求后的不同响应状态
        enum HTTP_CODE{
            NO_REQUEST,//表示服务器没有接收到完整的HTTP请求
            GET_REQUEST,//表示服务器已成功解析客户端的GET请求，并准备发送请求的资源
            BAD_REQUEST,//请求格式错误，服务器无法理解或无法处理请求
            NO_RESOURCE,//请求的资源不存在
            FORBIDDEN_REQUEST,//请求的资源不允许访问
            FILE_REQUEST,//请求的是文件资源 服务器已准备好发送文件
            INTERNAL_ERROR,//服务器内部错误 无法完成请求
            CLOSED_CONNECTION//表示连接已经被客户端关闭 或服务器决定关闭连接
        };
        //对HTTP请求解析过程中行状态的不同结果
        enum LINE_STATUS{
            LINE_OK=0,//表示成功读取并解释了一行 这意味着行以正确的格式结束（通常是'\r\n'），并且内容符合HTTP协议的要求  可以继续处理下一部分
            LINE_BAD,//表示行的格式错误或不完整 不能正确解析  需要向客户端返回错误消息
            LINE_OPEN//表示行数据尚未完成，可能因为数据被分割在多个网络数据包中 表面服务器需要继续读取网络数据，直到完成整行的接收
        };

    public:
        http_conn(){};//构造函数
        ~http_conn(){};//析构函数

    public:
        //init 函数 用于初始化一个 http_conn 对象，设置于特定客户端通信所需的所有参数
        //参数 sockdf 与客户端通信的套接字文件描述符   addr 客户端的地址信息 类型为sockaddr_in  
        //第三个参数指向服务器的根目录路径 用于文件访问  第四个参数为 触发模式 决定是使用水平触发还是边缘触发
        //第五个参数 是否关闭日志记录
        //最后三个参数 用于数据库连接的用户 密码 和数据库名称
        void init(int sockfd,const sockaddr_in &addr,char *, int, int, string user, string passwd, string sqlname);
        //关闭与客户端的连接，并根据 real_close 参数决定是否立刻关闭套接字
        //参数 real_close 一个布尔值 指示是否立刻关闭套接字， 如果为true，则关闭套接字 如果为false，保持套接字开放以支持HTTP持久连接
        void close_conn(bool real_close=true);
        //处理接收到的请求，通常涉及 解析HTTP请求 执行请求的操作 准备和发送HTTP响应
        void process();
        //尝试从套接字读取数据一次 通常被设计为非阻塞，即尝试读取当前可用的数据
        //返回true 表示成功读取到数据  false代表读取失败
        bool read_once();
        //向套接字写入准备好的响应数据 包括HTTP响应头、主体等
        //如果数据成功写入 返回true 如果写入失败返回false
        bool write();

        //get_address 返回一个指向 m_address 成员变量的指针  该函数提供了访问客户端地址信息的接口，这对于日志记录 响应处理或安全验证等功能是必要的
        //m_address 存储了当前HTTP连接的客户端网络地址(sockaddr_in 结构)
        sockaddr_in *get_address(){
            return &m_address;
        }
        //使用传入的数据库连接池初始化数据库查询信息 
        //参数 connPool 是一个指向connection_pool类实例的指针 
        void initmysql_result(connection_pool *connPool);

        int timer_flag;//控制超时 计时 或延迟操作
        int improv;//性能调优

    private:
        //内部初始化和处理函数
        void init();// 函数重载  一个私有初始化函数，用于设置或重置类内部的一些基础状态和变量
        HTTP_CODE process_read();//处理读操作，解析HTTP请求数据，根据请求的不同部分(请求行 请求头 请求体)调用响应的解析函数
        bool process_write(HTTP_CODE ret);//根据读取和解析的结果 HTTP_CODE,准备和发送 HTTP响应
        HTTP_CODE parse_request_line(char *text);//解析HTTP请求的请求行，确定请求方法 URL和协议版本
        HTTP_CODE parse_headers(char *text);//解析HTTP请求的头部字段
        HTTP_CODE parse_content(char *text);//处理HTTP请求的正文部分 通常用于POST请求
        HTTP_CODE do_request();//执行实际的请求处理，如访问文件 数据库操作等
        char *get_line(){return m_read_buf+m_start_line;};//返回指向当前处理的行在读缓冲区中的位置
        LINE_STATUS parse_line();//从读缓冲区中解析一行，用于请求解析
        void unmap();//取消映射 如果使用内存映射文件来发送文件，该函数取消映射
        
        //响应构建函数
        bool add_response(const char *format,...);//添加格式化的响应字符串到写缓冲区，支持可变参数
        bool add_content(const char *content);//向响应中添加具体内容
        bool add_status_line(int status,const char *title);//添加HTTP响应的状态行，包括状态码和描述文字
        bool add_headers(int content_length);//添加HTTP响应头 通常包括内容长度 连接状态等信息
        bool add_content_type();//添加相应的内容类型头部（如 'text/html'）
        bool add_content_length(int content_length);//添加 content_length头部，说明相应内容的长度
        bool add_linger();//添加 Connection 头部，通常用于指示连接是持久连接还是关闭
        bool add_blank_line();//响应头和响应体之间添加一个空白行，符合HTTP协议的要求

    public:
        //公共静态成员变量 被类所有实例共享
        static int m_epollfd;//存储epoll文件描述符
        static int m_user_count;//跟踪当前处理的用户连接数量
        //公共成员变量
        MYSQL *mysql;//一个指向MYSQL结构的指针 该成员变量使得 http_conn实例能够执行数据库查询喝其他数据操作
        int m_state;//用于标记当前HTTP连接的状态  0表示读状态(接收和解析客户端请求)  1表示写状态(发送响应到客户端)

     private:
        //基础连接管理
        int m_sockfd;//套接字文件描述符 用于管理与客户端的连接
        sockaddr_in m_address;//存储客户端地址的结构体 用于获取连接信息
        char *doc_root;//服务器的文档根目录路径  用于文件服务

        //缓冲区与索引
        char m_read_buf[READ_BUFFER_SIZE];//读缓冲区 用于存储从客户端接收到的数据
        long m_read_idx;//当前读缓冲区中数据的结束索引(从1开始，也即读缓冲区中有多少字符)
        long m_checked_idx;//已检查的数据位置索引
        int m_start_line;//当前解析行的起始位置
        char m_write_buf[WRITE_BUFFER_SIZE];//写缓冲区 用于存储要发送给客户端的数据
        int m_write_idx;//写缓冲区中的数据的结束索引(从1开始，也即写缓冲区中有多少字符)

        //HTTP请求解析
        CHECK_STATE m_check_state;//当前的请求解析状态
        METHOD m_method;//当前的请求方法
        char m_real_file[FILENAME_LEN];//请求的实际文件路径
        char *m_url;//请求的URL
        char *m_version;//HTTP版本
        char *m_host;//请求的主机名
        long m_content_length;//请求内容的长度
        bool m_linger;//是否保持连接
        char *m_file_address;//映射到内存中的文件地址
        struct stat m_file_stat;//文件的状态信息
        struct iovec m_iv[2];//用于输出的结构体数组 支持散布/聚集I/O
        int m_iv_count;//m_iv数组的元素数量

        //动态内容处理于状态管理
        int cgi;//标记是否为CGI请求，通常用于POST方法
        char *m_string;//存储请求头数据，用于处理POST请求

        //发送管理
        int bytes_to_send;//待发送数据的总字节数
        int bytes_have_send;//已发送数据的字节数

        //用户管理于数据库配置
        map<string,string> m_users;//存储用户名和密码的映射，用于认证
        char sql_user[100];//数据库用户名
        char sql_passwd[100];//数据库密码
        char sql_name[100];//数据库名

        //配置于日志
        int m_TRIGMode;//用于控制epoll触发模式
        int m_close_log;//是否关闭日志记录
};
#endif