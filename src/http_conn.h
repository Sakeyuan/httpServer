#pragma once
#include<sys/epoll.h>
#include<iostream>
#include<thread>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<string.h>
#include<sys/types.h>
#include<fcntl.h>
#include<errno.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<error.h>
#include<sys/uio.h>
#include"locker.h"

class http_conn{
public:
    static int m_epollfd;                       //所有socket的事件注册在一个epollfd时 
    static int m_user_nums;                     //统计用户数量
    static const int FILENAME_LEN = 200;
    static const int READ_BUF_SIZE=2048;        //读缓存大小
    static const int WRITE_BUF_SIZE=2048;
    

    //请求方法
    enum METHOD{GET = 0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT };

    //服务器主状态机解析状态
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT };

    //从状态机三种可能，即读取状态，分别表示
    //1.读取到一个完整的行LINE_OK   2.行出错LINE_BAD  3.行数据不完整LINE_OPEN
    enum LINE_STATE{LINE_OK =0,LINE_BAD,LINE_OPEN};

    /*  服务器解析结果
        NO_REQUEST          --报文不完整，需要继续读取客户数据
        GET_REQUEST         --获取了完整请求
        BAD_REQUEST         --请求报文语法错误
        NO_RESOURSE         --服务器没有资源
        FORBIDDEN_REQUST    --禁止访问资源
        FILE_REQUEST        --文件请求成功
        INTERNAL_ERROR      --服务器内部错误
        CLOSE_CONNECT       --客户端已关闭连接
    */
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURSE,FORBIDDEN_REQUST,FILE_REQUEST,INTERNAL_ERROR,CLOSE_CONNECT};
    
    //设置非阻塞
    void setnonblocking(int fd);  

    //初始化新接收的连接
    void init(int sockfd,const sockaddr_in& addr);  

    //非阻塞读数据
    bool read();   

    //非阻塞写数据
    bool write();      

    //关闭连接  
    void close_conn(); 

    //处理客户端请求
    void process(); 

private:

    int m_sockfd;                       //客户文件描述符

    sockaddr_in m_address;             //客户地址 

    char m_read_buf[READ_BUF_SIZE];     //读缓存区

    int m_read_idx;                     //读取指针位置

    char m_write_buf[WRITE_BUF_SIZE];   //写缓存区

    int m_write_idx;                    //写指针位置

    struct iovec m_iv[2];                //使用writev将不同位置的文件写入socket

    int m_iv_num;   

    int m_check_idx;                //报文解析指针位置

    int m_start_line;               //解析行起始位置

    char* m_url;                    //请求目标文件名

    char* m_version;                //协议版本

    METHOD m_method;                 //请求方法

    char* m_host;                   //主机名

    bool m_linger;                   //是否保持连接

    int m_content_length;           //请求体长度
    
    int bytes_to_send;              //要发送的字节数

    int bytes_have_send;            //已经发送字节数

    char root_path[FILENAME_LEN];     //资源根目录
    
    char m_real_file[FILENAME_LEN];     

    char* m_real_file_addr;           //请求文件地址

    struct stat m_file_stat;         //文件状态信息

    CHECK_STATE m_check_state;      //主状态机状态

    //初始化其余的数据
    void init();

    //解析HTTP请求
    HTTP_CODE process_read();

    //解析请求行
    HTTP_CODE parse_req_line(char* req_text);

    //解析请求头
    HTTP_CODE parse_req_header(char* req_text);

    //解析请求体
    HTTP_CODE parse_req_content(char* req_text);

    //从状态机单独解析一行
    LINE_STATE parse_line();

    //获取一行数据
    char* get_line(){ return m_read_buf + m_start_line;}

    //处理请求
    HTTP_CODE do_request();

    //处理响应
    bool process_write(HTTP_CODE ret);
    bool add_state_line(int state, const char* title);       //封装响应行（状态行）
    bool add_response( const char* format, ... );           
    bool add_headers(int content_len);                       //封装响应头
    bool add_content(const char* content);
    bool add_content_type();
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    void get_resources_path();

    //释放map映射
    void unmap();
};
