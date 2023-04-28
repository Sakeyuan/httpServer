#include"http_conn.h"
int http_conn::m_epollfd = -1;       //所有socket的事件注册在一个epollfd时 
int http_conn::m_user_nums = 0;     //统计用户数量

//http响应状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

void setnonblocking(int fd){
        int old_flag=fcntl(fd,F_GETFL);
        int new_flag=old_flag | O_NONBLOCK;
        fcntl(fd,F_SETFL,new_flag);
}

void add_fd(int epollfd,int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    //event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    
    setnonblocking(fd);
}

void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void http_conn::get_resources_path(){
    //构造资源路径
    getcwd(root_path,sizeof(root_path));
    char* pos=strstr(root_path,"/build/bin");
    if(pos){
        *pos = '\0';
    }
    strcat(root_path,"/resources");
}

void http_conn::init(int sockfd,const sockaddr_in& addr){
    m_sockfd = sockfd;
    m_address = addr;
    
    //初始化资源路径
    get_resources_path();
    
    //端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEPORT,&reuse,sizeof(reuse));

    //添加到
    add_fd(m_epollfd,m_sockfd,true);
    
    m_user_nums++;  //用户数+1

    init();  //初始化状态机
}   

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_check_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;
    m_content_length = 0;
    m_host = 0;
    m_write_idx = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;

    bzero(m_write_buf,WRITE_BUF_SIZE);
    bzero(m_real_file,FILENAME_LEN);
    bzero(m_read_buf,READ_BUF_SIZE);
}

bool http_conn::read(){  //一次性读
    if(m_read_idx >= READ_BUF_SIZE){
        return false;  //缓存区满
    }
    //已经读取字节
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUF_SIZE-m_read_idx,0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 没有数据可以读
                break;
            }
            return false;
        }
        else if(bytes_read == 0){  //对方关闭连接
            return false;
        }
        m_read_idx+=bytes_read;
    }
    printf("报文信息:\n%s\n",m_read_buf);
    return true;
}        

//线程池工作线程调用，处理http请求的入口函数
void http_conn::process(){
    //解析http请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        //继续监听
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return; 
    }
    
    //生成响应http报文
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}

//解析请求行
http_conn::HTTP_CODE http_conn::process_read(){
    HTTP_CODE ret = NO_REQUEST;           //主状态机初始状态
    LINE_STATE line_state = LINE_OK;      //从状态机初始状态

    char* text = 0;
    //逐行解析
    while((m_check_state == CHECK_STATE_CONTENT && line_state == LINE_OK) || (line_state = parse_line()) == LINE_OK){
            //解析到一行完整数据 ，或者解析到请求体，也算是完整数据
            //获取一行数据
            text = get_line();
            m_start_line = m_check_idx;  //读取一行后，将m_start_line放置到下一行开头，表示行的开始
            switch (m_check_state)
            {
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_req_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:{
                ret = parse_req_header(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT:{
                ret = parse_req_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_state = LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }      
        }
    }
    return NO_REQUEST;
}

//解析请求行,获取请求方法，目标URL，版本
http_conn::HTTP_CODE http_conn::parse_req_line(char* req_text){
    //GET /index.html HTTP/1.1
    m_url = strpbrk(req_text," \t");  //返回空格或者回车符第一次出现的索引
    
    *m_url++='\0';  //GET\0/ HTTP/1.1
    char* method=req_text;  //method = GET
    
    if(strcasecmp(method,"GET") == 0){  //忽略大小写比较
        m_method = GET;
    }else{
        return BAD_REQUEST;
    }
    
    m_version = strpbrk(m_url," \t"); //m_url = /index.html HTTP/1.1
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++='\0'; ///index.html\0HTTP/1.1
    // if(strcasecmp(m_version , "HTTP/1.1") != 0){
    //     return BAD_REQUEST;
    // }
     
    if(strncasecmp(m_url , "http://" , 7) == 0){  //http://192.168.130.192/index.html
        m_url+=7; 
        m_url=strchr(m_url,'/');  // /index.html
    }

    if(!m_url || (m_url[0] != '/')){
        return BAD_REQUEST;
    }
    if(strlen(m_url) == 1){
        strcat(m_url,"welcome.html");
    }
    m_check_state = CHECK_STATE_HEADER;  //主状态机状态变成检查请求头
   
    return NO_REQUEST;   //报文还没有解析完成
}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_req_header(char* req_text){
    if(req_text[0] == '\0'){ //遇到空行,表示头部字段解析完成
        if(m_content_length != 0){  //m_content_length表示请求体长度，如果没有请求体，则不需要解析请求体，已经解析完成
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; //解析完成
    }
    else if(strncasecmp(req_text,"Connection:",11) == 0){
        req_text+=11;
        req_text+=strspn(req_text," \t");  //返回不是 \t的下标
        if(strcasecmp(req_text,"keep-alive") == 0){
            m_linger = 0;
        }
    }
    else if(strncasecmp(req_text,"Host:",5) == 0){
        req_text+=5;
        req_text+=strspn(req_text," \t");
        m_host = req_text;
    }
    else if(strncasecmp(req_text,"Content-Length:",15) == 0){
        req_text+=15;
        req_text+=strspn(req_text," \t");
        m_content_length = atol(req_text);
    }
    else{
        //不处理不需要信息
        //printf("unknow header %s\n",req_text);
    }
    return NO_REQUEST;
}

//解析请求体,现在还没真正解析请求体消息，只是判断是否被完整读入了
http_conn::HTTP_CODE http_conn::parse_req_content(char* req_text){
    if(m_read_idx >= (m_content_length + m_check_idx)){
        req_text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//从状态机单独解析一行,判断依据\r\n
http_conn::LINE_STATE http_conn::parse_line(){
    char temp;
    for(; m_check_idx < m_read_idx ; ++m_check_idx){
        temp = m_read_buf[m_check_idx];
        if(temp == '\r'){
            if(m_check_idx+1 == m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_check_idx + 1] == '\n'){
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if(m_check_idx > 1 && m_read_buf[m_check_idx-1] == '\r'){
                m_read_buf[m_check_idx-1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//准备请求资源文件
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file,root_path);
    int len = strlen(root_path);
    //      /home/yuanjiafei/myWebServer/resources/index.html
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);   //构造资源文件具体地址
    if(stat(m_real_file,&m_file_stat) < 0){  //获取文件属性，存入m_file_stat
        return NO_RESOURSE;  //服务器没有资源
    }

    //判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUST;    //用户无访问权限
    }

    //判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    //以只读方式读取文件
    int fd = open(m_real_file,O_RDONLY);
    
    //创建文件映射 PROT_READ页内容可以被读取  MAP_PRIVATE建立一个写入时拷贝的私有映射,内存区域的写入不会影响到原文件
    m_real_file_addr = (char*)mmap(0,m_file_stat.st_size,PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap(){
    if(m_real_file_addr){
        munmap(m_real_file_addr,m_file_stat.st_size);
        m_real_file_addr = 0;
    }
}

//一次性写
bool http_conn::write(){ 
    int temp = 0;
    if(bytes_to_send == 0){              //没有要发送的数据
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while (true)
    {
        temp = writev(m_sockfd,m_iv,m_iv_num);
        if(temp <= -1){
            if(errno == EAGAIN){
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send-=temp;
        bytes_have_send+=temp;
        if(bytes_have_send >= m_iv[0].iov_len){
            //头文件发送完
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_real_file_addr+(bytes_have_send-m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if(bytes_to_send <= 0){
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            modfd(m_epollfd,m_sockfd,EPOLLIN);
            if(m_linger){
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}        

bool http_conn::add_response( const char* format, ... ){
    if(m_write_idx >= WRITE_BUF_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUF_SIZE-1-m_write_idx,format,arg_list);
    if( len >= ( WRITE_BUF_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx+=len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_state_line(int state, const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",state,title);
}

bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len){
    return add_response( "Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，判断给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
   
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_state_line(500,error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)){
            return false;
        }
        break;

    case BAD_REQUEST:
        add_state_line(400,error_400_title);
        add_headers(strlen(error_400_form));
        if(!add_content(error_400_form)){
            return false;
        }
        break;

    case NO_RESOURSE:
        add_state_line(404,error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form)){
            return false;
        }
        break;

    case FORBIDDEN_REQUST:
        add_state_line(403,error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)){
            return false;
        }
        break;
    
    case FILE_REQUEST:
        add_state_line(200,ok_200_title);
        add_headers(m_file_stat.st_size);

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_real_file_addr;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_num = 2;
        bytes_to_send = m_write_idx + m_file_stat.st_size;
        return true;
        break;

    default:
        return false;
        break;
    }
    //没有请求体
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_num = 1;
    return true;
}

//关闭链接 
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_nums--;
    }
}

