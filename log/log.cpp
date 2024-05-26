#include<string.h>
#include<time.h>
#include<sys/time.h>
#include<stdarg.h>
#include<pthread.h>
#include"log.h"

using namespace std;

//日志构造函数
Log::Log(){
    m_count=0;
    m_is_async=false;
}

//日志析构函数
Log::~Log(){
    //确保文件已经关闭
    if(m_fp!=nullptr){
        fclose(m_fp);
    }
}

//日志类初始化函数  配置日志系统的允许方式 同步或异步 以及日志文件的 名称和路径
bool Log::init(const char *file_name,int close_log,int log_buf_size,int split_lines,int max_queue_size){
    //如果设置了max_queue_size，则设置为异步模式 异步模式下 日志消息首先被放入一个阻塞队列中， 另一个独立的日志线程负责从队列中取出日志消息并写入文件
    //同步模式下 日志操作直接在主线程中执行，影响程序性能
    //设置异步模式   1先初始化一个阻塞队列 m_log_queue 2创建一个线程来处理队列中的日志消息
    if(max_queue_size>=1){
        m_is_async=true;//异步模式
        m_log_queue=new block_queue<string>(max_queue_size);//创建一个阻塞队列，用于储存日志消息
        //线程创建
        pthread_t tid;//定义变量 tid 用于存储新创建线程的标识
        //调用pthread_create 创建一个新线程   这个新线程将执行 flush_log_thread 函数 flush_log_thread函数作为异步模式下用于从队列中取出日志信息并写入日志文件的回调函数
        //pthread_create 第一个参数 是线程标识符的地址，用于记录新线程的信息
        //第二个参数 是线程属性，nullptr为默认线程属性
        //第三个参数 是线程函数 （一般返回类型为 void* 接受参数类型也为 void*）
        //第四个参数 是传递给线程函数的参数
        pthread_create(&tid,nullptr,flush_log_thread,nullptr);
    }
    ////初始化和配置
    //设置日志关闭标志
    m_close_log=close_log;
    //设置缓冲区大小并初始化缓冲区
    m_log_buf_size=log_buf_size;
    m_buf=new char[m_log_buf_size];//动态分配一个字符数组，大小为m_log_buf_size 这个缓冲区用于暂存即将写入文件的日志数据
    memset(m_buf,'\0',m_log_buf_size);//memset 函数初始化缓冲区，填充为 '\0'(空字符) 确保缓冲区清空 无残留数据
    //设置日志分割行数
    m_split_lines=split_lines;//控制单个日志文件的最大行数 超过该行数则分割日志文件，创建新的日志文件
    ////处理时间和文件名
    //获取并处理当前时间
    time_t t=time(nullptr);//获取当前的时间戳
    struct tm *sys_tm=localtime(&t);//将时间戳转换为本地实际的 tm 结构体
    struct tm my_tm=*sys_tm;//复制本地时间结构体，以便用于后续的日期处理
    //处理文件名和路径
    const char *p=strrchr(file_name,'/');//使用 strrchr 查找传入的文件名中最后一次出现 '/' 的位置，返回指向最后出现的'/'字符的指针
    char log_full_name[256]={0};//定义一个字符数组 log_full_name 作为完整的日志文件名，初始化所有元素为0  这个数组将来用作存放最终的带日期的完整文件路径名

    ////处理传入的日志文件名，并构建最终的日志文件名，其中包括日期信息
    //当传入文件名不包含路径
    if(p==nullptr){//意味着 file_name 中不包含 '/' 即文件名不包含路径信息
        //snprintf函数将 日期信息和文件名格式化到 log_full_name 中
        //my_tm.tm_year+1900 是因为 tm_year 是从1900年开始的年数
        //my_tm.tm_mon+1 是因为tm_mon 是从0 开始计数的 0代表1月
        //file_name 直接附加在日期后 因为不存在路径分割符需要处理
        //使用255个字符的限制是为了防止缓冲区溢出
        snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,file_name);
    }else{//表面file_name中至少有一个 '/'
        //提取文件名  strcpy函数 将路径中的 文件名部分 复制到 log_name中
        //p+1 指向 '/' 之后的第一个字符，即文件名的开始
        strcpy(log_name,p+1);
        //提取目录名 strncpy 函数从 file_name 中复制从开始到最后一个 '/'(包括该 '/')的部分到 dir_name 变量中
        //p-file_name+1 计算的是从文件名的起始到最后一个 '/'(包括该斜杠)的字符总数，确保目录名包括路径分隔符
        strncpy(dir_name,file_name,p-file_name+1);
        snprintf(log_full_name,255,"%s%d_%02d_%02d%s",dir_name,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,log_name);
    }
    //设置当前日期
    m_today=my_tm.tm_mday;
    //打开或创建日志文件
    //fopen 函数尝试以追加模式 "a" 打开日志文件，文件名由 log_full_name 指定
    //追加模式 如果文件已存在 写入操作将不会覆盖原有内容，而是在文件末尾追加新内容  如果文件不存在 则函数会创建新文件
    m_fp=fopen(log_full_name,"a");
    if(m_fp==nullptr){//检查 fopen 是否成功打开文件  若fopen返回nullptr 说明可能路径错误 权限不足 或磁盘空间不足
        return false;//文件打开失败 表明日志系统初始化未能成功完成
    }
    return true;//初始化完成 已经准备好记录日志
}

//处理异步和同步日志写入 并支持基于日期和日志条目数量自动分割日志文件 
void Log::write_log(int level,const char *format,...){
    struct timeval now={0,0};//定义一个 timeval 结构体 now 并初始化其秒和微妙字段为0
    gettimeofday(&now,nullptr);//调用gettimeofday函数获取当前的时间
    //时间格式转换
    time_t t=now.tv_sec;//将timeval 结构中的秒数部分赋值给time_t类型的变量t 用于转换成更易于处理的时间格式
    struct tm *sys_tm=localtime(&t);//将time_t类型的秒数转换为本地时间的tm结构体。 localtime返回一个指向静态分配的tm结构体的指针 该结构体包含了 年月日等
    struct tm my_tm=*sys_tm;//从localtime返回的指针中复制时间数据到局部变量my_tm 这样可以避免在多线程环境下对静态数据的竞争
    //设置日志级别标签
    char s[16]={0};//s字符数组用于存储日志级别的字符串，初始化为0，大小为16
    switch (level)
    {
    case 0:
        strcpy(s,"[debug]:");
        break;
    case 1:
        strcpy(s,"[info]:");
        break;
    case 2:
        strcpy(s,"[warn]:");
        break;
    case 3:
        strcpy(s,"[erro]:");
        break;
    default:
        strcpy(s,"[info]:");
        break;
    }

    ////文件分割检查和操作
    //锁定互斥量 共享资源包括 日志条目计数器 m_count 当前日志文件的日期 m_today 当前打开的日志文件的文件指针 m_fp
    m_mutex.lock();
    //每次写入日志时 日志计数器加一
    m_count++;
    //检查是否需要新的日志文件
    //检查当前日期是否与日志文件日期不同   检查当前日志行数是否已经达到最大行数 m_split_lines 满足任一条件需要分割日志文件
    if(m_today!=my_tm.tm_mday||m_count%m_split_lines==0){
        fflush(m_fp);//刷新文件流，确保所有缓冲的数据都被写入文件
        fclose(m_fp);//关闭当前日志文件

        ////创建新的日志文件
        //准备新文件的名称
        char new_log[256]={0};//定义并初始化新日志文件名的数组
        char tail[16]={0};//定义并初始化用于存放日期部分的数组
        snprintf(tail,16,"%d_%02d_%02d_",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday);
        //根据日期变化选择文件名
        if(m_today!=my_tm.tm_mday){//如果日期变量，意味着是新的一天
            snprintf(new_log,255,"%s%s%s",dir_name,tail,log_name);//创建不带分割序号的新日志文件名
            m_today=my_tm.tm_mday;//更新当前日志文件的日期
            m_count=0;//重置日志行计数器
        }else{//记录时间在同一天内，但是日志行数达到了限制
            snprintf(new_log,255,"%s%s%s.%lld",dir_name,tail,log_name,m_count/m_split_lines);//创建带分割序号的新日志文件名，序号为当前日志条目数除以最大行数
        }

        m_fp=fopen(new_log,"a");//以追加模式打开新的日志文件
    }

    //解锁互斥锁 完成了必须的同步操作之后(检查和更新日志文件),将允许其他线程可以进行日志写入操作，提高并发性能
    m_mutex.unlock();
    //处理可变参数
    va_list valst;//声明一个 va_list 类型的变量 valst 用于访问函数的可变参数列表
    va_start(valst,format);//初始化 valst 使其指向函数参数列表中的第一个可变参数 format是最后一个固定参数 va_start根据这个参数定位第一个可变参数

    //准备日志字符串
    string log_str;//log_str 存储最终格式化的日志文本
    //再次锁定互斥锁 前一步解锁是为了处理不需要同步的可变参数初始化 
    //在准备写入 日志字符串 到共享资源(如缓冲区或日志队列)之前，再次锁定互斥锁
    m_mutex.lock();

    //格式化日期和时间戳
    //snprintf将当前时间和日志级别标签格式化为字符串，并写入 m_buf字符数组 
    //snprintf返回值 n 是写入字符数组的字符数（不包括末尾的空字符）
    int n=snprintf(m_buf,48,"%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                    my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,
                    my_tm.tm_hour,my_tm.tm_min,my_tm.tm_sec,now.tv_usec,s);
    //格式化可变参数
    //vsnprintf格式化可变参数列表中的日志消息，并将结果追加到已有的时间戳字符串后
    //m_buf+n 表示指针从时间字符串的末尾开始写入 m_buf 中
    //m_log_buf_size-n-1 保证在数组的剩余空间中写入 预留一个字符位置给末尾的空字符
    //format 和 valst 是 vsnprintf 使用的格式字符串和对应的可变参数列表 format定义了输出格式 valst提供了填充这个格式的实际数据
    //vsnprintf返回值 m 是这次调用写入的字符数(不包括末尾的空字符)
    int m=vsnprintf(m_buf+n,m_log_buf_size-n-1,format,valst);
    //添加换行符和终结字符
    m_buf[n+m]='\n';//在格式化文本的末尾添加一个换行符
    m_buf[n+m+1]='\0';//在换行符之后添加一个空字符'\0'，以确保字符串的正确结束 防止缓冲区溢出
    //将字符数转换为字符串对象
    log_str=m_buf;//将m_buf中的内容赋值给log_str 操作涉及将m_buf中的字符拷贝到 log_str管理的内存中  std::string可以更安全的管理字符，避免直接操作裸字符数组带来的危险

    //在完成对贡献资源(如日志缓冲区)的修改之后，允许其他线程进行访问 避免死锁
    m_mutex.unlock();
    //根据配置写入日志
    if(m_is_async&&!m_log_queue->full()){
        //检查日志系统是否配置为异步模式   日志队列没有满   两个条件都满足，执行队列写入操作
        //将格式化好的日志字符串 log_str 推入日志队列 在异步模式下，一个单独的日志处理线程会从这个队列中取出日志数据并将其写入文件
        m_log_queue->push(log_str); 
    }else{
        //同步模式  或队列已满      直接写入文件
        m_mutex.lock();//锁定互斥锁，以安全的访问共享资源 如文件指针 m_fp
        fputs(log_str.c_str(),m_fp);//fputs 将日志字符串写入到文件
        m_mutex.unlock();//
    }
    va_end(valst);//清理可变参数列表的初始化 避免内存泄漏
}

//强制刷新 日志文件的 写入流缓冲区
void Log::flush(void){
    m_mutex.lock();//锁住 文件指针 m_fp 这类共享资源
    //强制刷新写入流缓冲区
    //fflush函数用于清空文件流的内部缓冲区 并将缓冲区内的数据 写入到关联的文件中
    fflush(m_fp);
    m_mutex.unlock();
}