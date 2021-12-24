# my-webserver

使用C++编写的简单的HTTP服务器。

## 功能

- 使用边缘触发的Epoll实现I/O多路复用，并使用模拟Proactor模式实现
- 使用多线程处理并发请求，并使用线程池避免频繁创建和销毁线程的开销
- 使用状态机解析HTTP请求
- 支持GET请求
- 基于最小堆来管理和关闭非活跃连接
- 使用智能指针来减少内存泄漏
- 使用单例模式实现了一个简单的异步日志系统
- 实现了优雅关闭连接
- 经过Webbench压力测试，可以支持上万并发连接的数据交换

## 使用方法

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
- `-v` or `--version`: 版本信息
- `-h` or `--help`: 帮助信息

### 压力测试

```bash
cd test_pressure/webbench-1.5
make
# ./webbench -c [connections] -t [timeout] --http11 http://ip:pos/index.html
# default
./webbench -c 10000 -t 5 --http11 http://ip:port/index.html
```

## TODO

- 增加对POST请求的支持
- 使用Reactor模式实现本服务器
