# httpServer
--线程池+epoll
--模拟同步I/O的proactor模式

    --目录结构
        ├── 请求报文.txt
        ├── 响应报文.txt
        ├── build
        ├── CMakeLists.txt
        ├── myWebServer.docx
        ├── README.md
        ├── resources
        │   ├── images
        │   ├── index.html
        │   ├── rabbit.mp4
        │   └── welcome.html
        ├── src
        │   ├── CMakeLists.txt
        │   ├── http_conn.cpp
        │   ├── http_conn.h
        │   ├── locker.cpp
        │   ├── locker.h
        │   ├── main.cpp
        │   ├── thread_pool.h
        │   ├── webServer.cpp
        │   └── webServer.h
        ├── tree.txt
        └── webbench-1.5

项目简介

服务器是一个http服务器，主线程使用epoll和LT模式通过监听服务器指定的socket是否有事件发生，有事件发生epoll_wait 通知主线程，如果事件是客户端的连接，就将用户的信息保存起来；如果事件是可读事件或者可写事件，使用的是同步 I/O 方式模拟Proactor 模式，主线程负责读和写相应socket的数据，将浏览器发送过来的数据读取到m_read_buf读缓冲区中，读取完成后，主线程通过 add() 方法将请求添加到队列中，主线程和所有子线程通过共享一个请求队列 m_work_queue来同步，并用互斥锁保证线程安全。子线程都睡眠在请求队列上，通过信号量控制。

任务被添加到队列，则信号量加1

    任务队列：@1->@2->@3

信号量减1，子程从任务队列取出任务进行处理。子线程主要负责解析http请求报文，并准备请求的数据，http响应行，响应头放置在m_wirte_buf缓冲区，响应体使用mmap映射，数据准备好后，epoll触发可写事件，主线程使用writev将数据发送给请求端。

 


