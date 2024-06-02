#makefile 文件包含一系列的规则来指定如何编译源代码并链接生成最终的可执行文件

#变量定义 
CXX?=g++  #定义一个变量'CXX',用来指定编译器。'?='表示只有当'CXX'未被之前的环境或命令行定义时才使用'g++'作为默认值
DEBUG?=1  #变量定义 设置DEBUG变量，默认是'1'。这通常用来控制编译时的调试模式

#条件判断     根据DEBUG变量的值改变编译选项  
ifeq($(DEBUG),1)              #DEBUG=1,则在CXXFLAGS变量中加入'-g',这是gcc/g++编译器的调试信息选项，用于生成调试信息
	CXXFLAGS += -g        
else                           #DEBUG！=1,则在CXXFLAGS变量中加入'-02'，这是一个优化等级，告诉编译器进行适度的优化
	CXXFLAGS += -02
endif   

#编译规则
server: main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp webserver.cpp config.cpp
		$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient

#这条规则定义了如何构建目标 server，依赖于列出的.cpp文件   '$^' 是一个自动变量，代表所有的依赖项(这里是所有的'.cpp'文件)
#$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient  : 这是一个命令，使用变量'CXX'(编译器)来编译所有的'.cpp'文件，链接生成可执行文件'server'
#'$(CXXFLAGS)':包含了编译选项  '-lpthread':链接POSIX线程库  '-lmysqlclient'：链接MySql客户端库
 




#清理规则    'rm -r server'：用于删除生成的可执行文件 'server'
clean:
	rm -r server