# WebServer
A tiny web server project following a GitHub tutorial

This project is a personal learning exercise based on the original project by [qingguoyi](https://github.com/qinguoyi/TinyWebServer)

## License
This project is licensed under the Apache License 2.0 See the [LICENSE](./LICENSE) file for details.


======================================
客户端和服务器
>* 客户端：如Web浏览器等其他客户端软件通过网络向服务器发起请求，请求可以是获取网页 提交表单数据等
>* 服务器:通过Webserver类实现监听特定端口上的入站请求，处理来自客户端的HTTP请求，并返回响应。这包括解析请求 访问资源或执行其他操作 如数据库查询

服务器与数据库
>* 服务器使用数据库来存储和检索数据，这对于动态Web应用尤其重要。例如，用户信息 交易数据或任何其他需要持久化存储的数据都可以存储在数据库中
>* 数据库操作通常通过sql执行，这是一种用于管理关系数据库和执行各种数据操作的标准语言

数据库连接池
>* connection_pool *m_connPool 成员变量表示数据库连接池，这是一种性能优化技术。连接池维护了一组已经建立的数据库连接，可以被多个客户端请求重复利用
    而不是每次请求都建立新连接
>* 当服务器需要访问数据库执行SQL操作时，它可以从连接池中获取一个现有的连接，使用完毕后再将连接返回池中。这减少了连接创建和销毁的开销，提高了应用程序
    的响应速度和可扩展性

用户账号和密码
>* 用户账号和密码用于身份验证，确保只有合法用户可以访问特定的饿数据或执行特定的操作。这些信息通常存储在数据库中，安全性非常重要
>* 在Webserver类中，m_user和m_password变量分别存储数据库登陆所需的用户名和密码。服务器使用这些凭证来访问数据库，执行查询和其他操作

SQL
>* SQL用于服务器与数据库之间的通信。

服务器与线程池
>* 服务器使用线程池来管理并发处理多个客户端的请求 每个线程可以独立处理一个或多个请求，这样可以显著提高服务器处理大量并发请求的能力

线程池与数据库连接池
>* 在处理需要访问数据库的请求时，线程池中的线程将利用数据库连接池。数据库连接池允许线程快速获取已经建立的数据库连接，而无需每次请求时都进行连接和断开，
    这大大提高了数据库操作的效率
>* 线程池中的每个线程可能需要执行SQL查询或更新操作，这些操作通过从连接池中获取的连接执行



==============================================
int setsockopt(int socket,int level, int option_name, const void *option_value, socklen_t option_len); 修改套接字的行为
参数解释：
>* socket:需要被设置或者获取选项的套接字描述符
>* level:选项所在的协议层 对于通用套接字级别的选项，设置为SOL_SOCKET  如果是针对特定协议的选项 如TCP，就应该设置为对应协议的标识，例如IPPRPTP_TCP
>* option_name:想要访问的选项名 例如SO_LINGER是一种选项，用来控制套接字关闭的行为
>* option_value:指向包含新选项值的缓冲区的指针
>* option_len：传入的选项值的大小
