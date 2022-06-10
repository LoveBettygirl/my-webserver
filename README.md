# my-webserver

使用C++编写的简单的HTTP服务器。

## 功能

- 使用边缘触发的Epoll实现I/O多路复用，并使用模拟Proactor模式实现
- 使用多线程处理并发请求，并使用线程池避免频繁创建和销毁线程的开销
- 支持GET、POST、HEAD请求，并使用主从状态机解析HTTP请求，优化请求体处理逻辑，以支持POST请求处理
- 支持服务器验证以及CGI两种实现POST请求的方式
- 基于最小堆来管理和关闭非活跃连接
- 使用智能指针来减少内存泄漏
- 使用单例模式实现了一个简单的异步日志系统
- 实现了优雅关闭连接
- 实现了自动增长的缓冲区，以支持大文件上传的下载
- 实现了简单的用户注册、登录功能，可以请求服务器图片和视频文件
- 使用Redis实现了简单的Cookie和Session功能，记住用户的登录状态，缓存用户信息，并能轻松扩展到集群部署
- 使用数据库连接池降低频繁建立和释放数据库连接的开销，并通过RAII机制（`unique_ptr`）实现连接自动回收
- 经过Webbench压力测试，可以支持上万并发连接的数据交换
- 提供配置文件，方便用户进行个性化配置

## 依赖

- gcc（>=7.5）
- MySQL（>=5.7）
- Redis（对应的C++开发库为[hiredis](https://github.com/redis/hiredis)）
- Python（>=3.6，一般Linux系统都有内置）

## 使用方法

### cgi文件配置

为了确保提供的cgi程序能正常运行（在 `resources/cgi-bin/` 中），需要对其中的文件进行提权：

```bash
chmod -R 777 resources/cgi-bin/
```

### MySQL配置

在MySQL的 `root` 用户下（默认使用 `root` 用户，如需更换必须修改配置文件 `server.conf`）：

```sql
create database mydb;
USE mydb;
CREATE TABLE user(
    username char(50) NULL,
    password char(50) NULL
) ENGINE=InnoDB;
INSERT INTO user(username, password) VALUES('name', 'passwd');
```

### 构建项目

```bash
make
```

### 运行

```bash
./server [options]
```

命令行选项：

- `-p PORT` or `--port=PORT`: 指定服务器运行的端口号
- `-r PATH` or `--doc_root=PATH`: 指定服务器使用的资源根目录
- `-c` or `--close_log`: 关闭日志输出
- `-d` or `--daemon`: 以守护进程运行
- `-t NUM` or `--thread_pool_size=NUM`: 指定线程池的线程数量
- `-s NUM` or `--connection_pool_size=NUM`: 指定连接池的连接数量
- `-i CONFIG_FILE` or `--config=CONFIG_FILE`: 指定配置文件，格式见 `server.conf`，可指定 `server.conf` 作为配置文件。**如果需要更换数据库连接的用户、密码、数据库名等，必须指定配置文件。**
- `-v` or `--version`: 版本信息
- `-h` or `--help`: 帮助信息

### 功能测试

例如服务器运行在 `10000` 端口上，在浏览器中输入 <http://127.0.0.1:10000> 即可访问首页，进行登录、GET/POST请求以及文件上传功能测试。

### 压力测试

```bash
cd test_pressure/webbench-1.5
make
# ./webbench -c [connections] -t [timeout] --http11 http://ip:port/index.html
# default
./webbench -c 10000 -t 5 --http11 http://ip:port/index.html
```

## TODO

- 仿照[Muduo](https://github.com/chenshuo/muduo)，使用主从Reactor模式实现本服务器
- 继续重构代码
