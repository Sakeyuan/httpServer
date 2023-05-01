#include"webServer.h"

#define LT
//#define ET
webServer::webServer(int port){
    //端口号
    m_port = port;
    m_stop = false;

    //信号处理
    add_sig(SIGPIPE,SIG_IGN);
    add_sig(SIGINT,sig_handler);
    add_sig(SIGTSTP,sig_handler);
    
    //初始化线程池
    try{
        pool = new thread_pool<http_conn>;
    }catch(...){
        exit(-1);
    }
    
    //保存客户端信息
    user =new http_conn[MAX_FD];

    listenfd = socket(PF_INET,SOCK_STREAM,0);

    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEPORT,&reuse,sizeof(reuse));

    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=INADDR_ANY;
    serv_addr.sin_port=htons(m_port);

    if(bind(listenfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) ==-1){
        throw std::exception();
    }

    if(listen(listenfd,5) == -1){
        throw std::exception();
    }

    epollfd = epoll_create(5);

    //将监听的文件描述符添加到epoll对象中
    add_fd(epollfd,listenfd,false);

    //创建管道
    if(socketpair(PF_UNIX,SOCK_STREAM,0,pipefd) == -1){
        printf("socketpair error");
        throw std::exception();
    }
    setnonblocking(pipefd[1]);
    add_fd(epollfd,pipefd[0],false);
    http_conn::m_epollfd = epollfd;
}
  
void webServer::start(){
    while (!m_stop)
    {
        int nums = epoll_wait(epollfd,events,MAX_EVENT_NUM,-1);
        
        if(nums < 0 && errno != EINTR){
            throw std::exception();
        }

        for (int i = 0; i < nums; i++)
        {
            int sockfd=events[i].data.fd;
            if(sockfd == listenfd){     //产生客户端连接
                socklen_t clnt_addr_sz = sizeof(listenfd); 
        #ifdef LT
                int connfd = accept(listenfd,(struct sockaddr*)&clnt_addr,&clnt_addr_sz);
                if(connfd < 0){
                    // 已经没有连接请求，跳出循环
                    printf("已经没有连接请求\n");
                    continue;
                }
                if(http_conn::m_user_nums >= MAX_FD){  //连接满
                    //服务器正忙
                    printf("服务器正忙\n");
                    close(connfd);
                    continue;
                }
                //将新客户初始化
                user[connfd].init(connfd,clnt_addr);  
        #endif 

        #ifdef ET
                while (true)
                {
                    int connfd = accept(listenfd,(struct sockaddr*)&clnt_addr,&clnt_addr_sz);
                    if(connfd < 0){
                        // 已经没有连接请求，跳出循环
                        printf("已经没有连接请求\n");
                        break;
                    }
                    if(http_conn::m_user_nums >= MAX_FD){  //连接满
                        //服务器正忙
                        printf("服务器正忙\n");
                        close(connfd);
                        break;
                    }
                    //将新客户初始化
                    user[connfd].init(connfd,clnt_addr);   
                }
                continue;
        #endif
            }

            //信号处理
            else if((events[i].events & EPOLLIN) && (sockfd == pipefd[0])){
                int sig = 0;
                char signals[1024];
                int ret = recv(pipefd[0],signals,sizeof(signals),0);
                if(ret == -1 || ret == 0){
                    continue;
                } 
                else{
                    for(int i=0;i<ret;++i){
                        switch (signals[i])
                        {
                        case SIGINT:{
                            m_stop = true;
                            break;
                        }
                        case SIGTSTP:{
                            m_stop = true;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }

            //读事件
            else if(events[i].events & EPOLLIN){
                //有数据可以读
                if(user[sockfd].read()){
                    //一次性将数据读取完成
                    pool->add(user+sockfd);
                }else{
                    user[sockfd].close_conn();
                }
            }

            //写事件，一次性写完
            else if(events[i].events & EPOLLOUT){  
                if(!user[sockfd].write()){
                    user[sockfd].close_conn();
                }
            }
            
            //客户端关闭连接或者异常
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){  
                //客户端异常断开或者错误事件
                user[sockfd].close_conn();
            }
        }
    }
}

webServer::~webServer(){
    close(epollfd);
    close(listenfd);
    delete []user;
    delete pool;
}

void webServer::add_sig(int sig,void(handler)(int)){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);  //设置信号集
    sigaction(sig,&sa,NULL);
}

void sig_handler(int sig){
    //保证函数的可重入性，保留线程进入前的errno
    int old_errno = errno;
    send(pipefd[1],(char*)&sig,1,0);
    errno = old_errno;
}
        







    



