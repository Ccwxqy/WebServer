
定时器处理非活动连接
==================
由于非活跃连接占用了连接资源，严重影响服务器的性能，通过实现一个服务器定时器，处理这种非活跃连接，释放连接资源。利用alarm函数周期性地触发SIGALRM信号，
该信号的信号处理函数利用管道通知主循环执行定时器链表上的定时任务
>*统一事件源
>*基于升序链表的定时器
>*处理非活动连接

========================
结构体 client_date  
>* 客户端的地址信息
>* 于客户端通信的套接字文件描述符
>* 指向一个定时器，用于管理于该客户端相关的定时任务

定时器类 util_timer
>* time_t expire ：定时器的到期时间
>* void(* cb_func)(client_data *):定时器到期时调用的回调函数，参数为客户端数据
>* client_data *user_data:指向使用该定时器的客户端数据的指针
>* util_timer *prev, *next:指向链表中前一个和后一个定时器的指针，用于在链表中维护定时器的顺序

管理定时器的有序链表类 sort_timer_lst
>* 添加定时器到链表中
>* 调整链表中定时器的位置 保证链表的顺序
>* 从链表中删除一个定时器
>* 定期检查链表，执行到期的定时器，并从链表中删除

提供与网络相关的实用功能类 Utils
>* 初始化定时器处理机制
>* 将文件描述符设置为非阻塞模式
>* 将文件描述符添加到epoll事件监听中
>* 信号处理函数
>* 添加信号处理函数
>* 处理定时事件
>* 显示错误信息



=================================
addsig(int sig, void(handler)(int), bool restart=true)：用于设置特定信号sig的处理函数。它接受一个信号代码、一个信号处理函数handler作为参数，并且可以指定在信号处理函数执行后是否重新启动被该信号中断的系统调用(通过restart参数)

static void sig_handler(int sig):是一个静态成员函数 用作信号的实际处理函数。 当特定信号被触发时，这个函数被调用

void timer_handler()：用于处理与定时器相关的逻辑。通常包括重新设置定时器以周期性地触发信号，从而使得服务器可以周期性地执行维护任务，如检查超时连接

void cb_func(client_data *user_data)：cb_func是一个回调函数，由util_timer的实例在定时事件发生时调用。

///////////////               逻辑关系                ///////////////////
1 信号和定时器的设置
>* 使用addsig函数注册sig_handler作为处理SIGALRM(或其他信号)的函数
>* 通过timer_handler方法设置和管理SIGALRM的触发，确保服务器按预定的时间间隔处理定时任务

2 信号触发和响应
>* 当SIGALRM触发时，操作系统调用sig_handler
>* sig_handler根据信号类型执行相应的操作，如果是定时信号，它可能会调用timer_handler来处理和重置定时器

3 定时器和回调的执行
>* 在timer_handler中，会调用sort_timer_lst的tick()方法，后者遍历定时器列表，检查并触发到期的定时器
>* 当定时器到期时，util_timer实例的cb_func被调用，传入与定时器关联的client_data，以执行具体的业务逻辑

============================
回调函数：本质上是一个通过函数指针调用的函数。可以将一个函数的地址(指针)传递给另一个函数，参数为一个函数的指针的函数可以在适当的时候调用这个通过指针指定的函数(就是参数)
在util_timer结构体中有一个成员 void (*cb_func)(client_data *)。这是一个函数指针，它指向一个接受 client_data类型参数的函数。当定时器到期时，通过这个函数指针调用相应的函数，
处理与该定时器相关的特定任务

示例：
 一个用于处理网络超时的函数
    void handle_timeout(client_data *data){
        close(data->sockfd);//关闭连接，记录日志等操作
        std::cout<<"Connection timed out for socket"<<data->sockfd<<std::endl;
    }
在设置定时器时，可以指定调回函数
util_timer *timer= new util_timer();
timer->cb_func=handle_timeout;
timer->user_data=&some_client_data;

当定时器到期并且tick方法被调用时，如果发现timer到期，它将调用handle_timeout,传递some_client_data作为参数



========================================
send函数：用于发送数据到指定的socket，可以用于任何类型的文件描述符 如管道
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
参数解释：
>* sockfd:发送数据的文件描述符
>* buf:指向包含待发送数据的缓冲区的指针
>* len:要发送的数据字节数
>* falgs:控制发送行为的各种标志 例如MSG_DONTWAIT非阻塞发送

read函数：用于从文件描述符中读取数据
ssize_t read(int fd, void *buf, size_t count);
参数解释：
>* fd:要读取数据的文件描述符
>* buf:指向缓冲区的指针，用于存放读取的数据
>* count:要读取的最大字节数

=========================================
管道：用于进程间通信的方法，运行一个进程的输出直接成为另一个进程的输入，通常有两个文件描述符
>* u_pipefd[0]:用于读取管道的内容，这是管道的读端
>* u_pipefd[1]:用于向管道写入内容，这是管道的写端；

==================================
sigaction函数 用于检查或修改信号处理方式的Unix系统调用
int sigaction(int signum,const struct sigaction *act, struct sigaction *oldact);  成功返回0  失败返回-1 并设置errno
参数解释：
>* signum:要操作的信号编号 如 SIGINT 或 SIGTERM
>* act:指向sigaction结构体的指针，该结构体指定了新的信号处理方式
>* oldact:用于保存旧的信号处理方式的sigaction结构体

sigaction结构体包含
>* sa_handler:是一个函数指针，指向信号处理函数
>* sa_mask:是一个信号集，指定在处理该信号时哪些信号应该被阻塞
>* sa_flags:用于指定信号处理的各种选项，如 SA_RESTART


sigfillset函数 用于初始化信号集，使其包含所有已定义的信号的函数
int sigfillset(sigset_t *set);  成功返回0   失败返回-1
参数解释：
>* set:指向sigset_t数据结构的指针，该结构代表一个信号集


assert 用于在运行时进行断言检查的宏   
==================================
alarm函数 用于设置一个定时器，该定时器在指定的秒数后将SIGALRM信号发送给当前进程。如果定时器到期前再次调用 alarm，任何之前的定时器都会被新的调用所取代
unsigned int alarm(unsigned int seconds);  返回之前设置的定时器的剩余时间(如果有的话),单位是秒
seconds :定时器触发前等待的时间，单位为秒
