#pragma once
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<exception>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include"locker.h"
#include"thread_pool.h"
#include"http_conn.h"
#define MAX_FD 65535                //最大客户端数
#define MAX_EVENT_NUM 1000          //最大监听数量

//添加描述符到epollfd中
extern void add_fd(int epollfd,int fd, bool one_shot);

//将fd从epollfd删除描述符
extern void removefd(int epollfd,int fd);

//修改文件描述符,重置socket上的EPOLLONESHOT，确保下次数据来时触发EPOLLIN
extern void modfd(int epollfd,int fd,int ev);

//信号处理
extern void sig_handler(int sig);

//设置非阻塞
extern void setnonblocking(int fd);

//信号管道
static int pipefd[2];


class webServer{
public:
    webServer(int port);
    ~webServer();
    void start();
    void add_sig(int sig,void(handler)(int));
    
public:

    int m_port;  //端口号

    int m_stop;  //服务器开启标识
    
    thread_pool<http_conn>* pool = NULL;       //线程连接池

    struct sockaddr_in serv_addr;           //服务器地址

    struct sockaddr_in clnt_addr;           //客户端地址

    int listenfd;                           //客户端文件描述符

    http_conn* user;                        //连接用户

    int epollfd;
        
    epoll_event events[MAX_EVENT_NUM];
};
