#include "http_conn.h"

void setnonblocking(int socketfd)
{
    int old_opt = fcntl(socketfd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(socketfd, F_SETFL, new_opt);
}

void addfd(int epollfd, int socketfd)
{
    //把该描述符添加到epoll的事件表
    epoll_event m_event;                                   //新建一个事件
    m_event.data.fd = socketfd;                            //使得描述符为本描述符
    m_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;       //监听输入、边缘触发、挂起？ 知识点，什么叫挂起？
    epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &m_event); //将该事件添加，知识点，ctl的作用？
    setnonblocking(socketfd);                              //设置非阻塞，什么叫阻塞非阻塞
}

void removefd(int epollfd, int socketfd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, socketfd, 0); //把该套接字的对应事件删除
    close(socketfd);
}

void modfd(int epollfd, int socketed, int ev)
{
    epoll_event m_event;
    m_event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    m_event.data.fd = socketed;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, socketed, &m_event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::init(int socketfd, const sockaddr_in &addr) //套接字的初始化，用于添加进epoll的事件表
{
    m_socket = socketfd;
    m_addr = addr;
    addfd(m_epollfd, m_socket);
    init();
}

void http_conn::init()
{
    filename = "";
    memset(read_buff, '\0', BUFF_READ_SIZE); //清空缓冲区
    memset(write_buff, '\0', BUFF_WRITE_SIZE);
    read_for_now = 0;
    write_for_now = 0;
}

void http_conn::close_conn(string msg)
{
    //将当前的描述符在epoll监听事件里面去除
    if (m_socket != -1)
    {
        removefd(m_epollfd, m_socket);
        m_user_count--;
        m_socket = -1; //-1就是代表没有正在连接的套接字
    }
}

void http_conn::process() //对请求进行处理
{
    //首先进行报文的解析
    HTTP_CODE ret = process_read();
    if (ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_socket, EPOLLIN);
        return;
    }
    //然后进行报文的响应
    bool result = process_write(ret);
    modfd(m_epollfd, m_socket, EPOLLOUT); //最后向epoll的监听的事件表中添加可写事件
}

void http_conn::parser_requestline(const string &text, map<string, string> &m_map)
{
    string m_method = text.substr(0, text.find(" "));
    string m_url = text.substr(text.find_first_of(" ") + 1, text.find_last_of(" ") - text.find_first_of(" ") - 1);
    string m_protocol = text.substr(text.find_last_of(" ") + 1);
    m_map["method"] = m_method;
    m_map["url"] = m_url;
    m_map["protocol"] = m_protocol;
}
void http_conn::parser_header(const string &text, map<string, string> &m_map)
{
    if (text.size() > 0)
    {
        if (text.find(": ") <= text.size())
        {
            string m_type = text.substr(0, text.find(": "));
            string m_content = text.substr(text.find(": ") + 2);
            m_map[m_type] = m_content;
        }
        else if (text.find("=") <= text.size())
        {
            string m_type = text.substr(0, text.find("="));
            string m_content = text.substr(text.find("=") + 1);
            m_map[m_type] = m_content;
        }
    }
}

void http_conn::parser_postinfo(const string &text, map<string, string> &m_map)
{
    //username=chaishilin&passwd=12345&votename=alibaba
    //cout << "post:   " << text << endl;
    string processd = "";
    string strleft = text;
    while (true)
    {
        processd = strleft.substr(0, strleft.find("&"));
        m_map[processd.substr(0, processd.find("="))] = processd.substr(processd.find("=") + 1);
        strleft = strleft.substr(strleft.find("&") + 1);
        if (strleft == processd)
            break;
    }
}

http_conn::HTTP_CODE http_conn::process_read()
{

    string m_head = "";
    string m_left = read_buff; //把读入缓冲区的数据转化为string
    //cout << read_buff << endl;
    //cout << "--------------------" << endl;
    int flag = 0;
    int do_post_flag = 0;
    while (true)
    {
        //对每一行进行处理
        m_head = m_left.substr(0, m_left.find("\r\n"));
        m_left = m_left.substr(m_left.find("\r\n") + 2);
        if (flag == 0)
        {
            flag = 1;
            //cout << "request line : " << m_head << endl;
            parser_requestline(m_head, m_map);
        }

        else if (do_post_flag)
        {
            //cout << "do: post" << endl;
            parser_postinfo(m_head, m_map);
            break;
        }
        else
        {
            //cout << "head : " << m_head << endl;
            parser_header(m_head, m_map);
        }
        if (m_head == "")
            do_post_flag = 1;
        if (m_left == "")
            break;
    }
    /*
    cout << "map:--------------------------------------------------------------------" << endl;
    for (auto i : m_map)
    {
        if(i.first == "username" || i.first =="passwd")
            cout << i.first << ":" << i.second << endl;
    }
    cout << "mapok:+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl;
    //favicon.ico
    */
    if (m_map["method"] == "POST")
    {
        //cout << "request" << read_buff << endl;
        return POST_REQUEST;
    }
    else if (m_map["method"] == "GET")
    {
        return GET_REQUEST;
    }
    else
    {
        return NO_REQUEST;
    }
}

bool http_conn::do_request()
{
    //区分get和post都请求了那些文件或网页
    //cout << "method: " << m_map["method"] << " url: " << m_map["url"] << endl;
    if (m_map["method"] == "POST")
    {
        redis_clt *m_redis = redis_clt::getinstance();
        if (m_map["url"] == "/base.html" || m_map["url"] == "/") //如果来自于登录界面
        {
            //处理登录请求
            //cout << "处理登录请求" << endl;
            //cout << "user's passwd: " << m_redis->getUserpasswd(m_map["username"]) << endl;
            //cout << "we got : " << m_map["passwd"] << endl;
            if (m_redis->getUserpasswd(m_map["username"]) == m_map["passwd"])
            {
                //cout << "登录进入欢迎界面" << endl;
                if (m_redis->getUserpasswd(m_map["username"]) == "root")
                    filename = "./root/welcomeroot.html"; //登录进入欢迎界面
                else
                    filename = "./root/welcome.html"; //登录进入欢迎界面
            }
            else
            {
                //cout << "登录失败界面" << endl;
                filename = "./root/error.html"; //进入登录失败界面
            }
        }
        else if (m_map["url"] == "/regester.html") //如果来自注册界面
        {
            m_redis->setUserpasswd(m_map["username"], m_map["passwd"]);
            //cout << "set:" << m_map["username"]<<"passwd:  "<<m_map["passwd"] << endl;
            filename = "./root/regester.html"; //注册后进入初始登录界面
        }
        else if (m_map["url"] == "/welcome.html") //如果来自登录后界面
        {
            m_redis->vote(m_map["votename"]);
            filename = "./root/welcome.html"; //进入初始登录界面
        }
        else if (m_map["url"] == "/getvote") //如果主页要请求投票
        {
            //读取redis
            postmsg = m_redis->getvoteboard();
            //cout << postmsg << endl;
            return false;
        }
        else
        {
            filename = "./root/base.html"; //进入初始登录界面
        }
    }
    else if (m_map["method"] == "GET")
    {
        if (m_map["url"] == "/")
        {
            m_map["url"] = "/base.html";
        }
        filename = "./root" + m_map["url"];
    }
    else
    {
        filename = "./root/error.html";
    }
    return true;
    //cout << "return filename : " << filename << endl;
}
void http_conn::unmap()
{
    if (file_addr)
    {
        munmap(file_addr, m_file_stat.st_size);
        file_addr = 0;
    }
}
bool http_conn::process_write(HTTP_CODE ret)
{

    if (do_request())
    {
        //filename
        int fd = open(filename.c_str(), O_RDONLY);

        stat(filename.c_str(), &m_file_stat);
        file_addr = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        m_iovec[1].iov_base = file_addr;
        m_iovec[1].iov_len = m_file_stat.st_size;
        close(fd); //居然忘记关闭描述符了
    }
    else
    {

        //postmsg
        
        strcpy(post_temp, postmsg.c_str());
        //cout <<postmsg.size()<<" :" << post_temp << endl;
        m_iovec[1].iov_base = post_temp;
        m_iovec[1].iov_len = (postmsg.size()) * sizeof(char);
        
        //filename
        /*
        int fd = open(filename.c_str(), O_RDONLY);

        stat(filename.c_str(), &m_file_stat);
        file_addr = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        m_iovec[1].iov_base = file_addr;
        m_iovec[1].iov_len = m_file_stat.st_size;
        cout << file_addr << endl;
        close(fd); //居然忘记关闭描述符了
        */
    }

    return true;
}

bool http_conn::read() //把socket的东西全部读到读缓冲区里面
{
    if (read_for_now > BUFF_READ_SIZE) //如果当前可以写入读缓冲区的位置已经超出了缓冲区长度了
    {
        cout << "read error at beigin" << endl;
        return false;
    }
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_socket, read_buff + read_for_now, BUFF_READ_SIZE - read_for_now, 0);
        if (bytes_read == -1) //出现错误
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) //如果是因为。。。和，，，原因导致的话，就顺利退出循环
            {
                break;
            }
            cout << "bytes_read == -1" << endl;
            return false;
        }
        else if (bytes_read == 0) //没东西可读？
        {
            cout << "bytes_read == 0" << endl;
            return false; //读完了为什么返回0？
            continue;
        }
        read_for_now += bytes_read;
    }

    return true;
}

bool http_conn::write() //把响应的内容写到写缓冲区中,并説明该连接是否为长连接
{
    int bytes_write = 0;
    //先不考虑大文件的情况
    string response_head = "HTTP/1.1 200 OK\r\n\r\n";
    char head_temp[response_head.size()];
    strcpy(head_temp, response_head.c_str());
    m_iovec[0].iov_base = head_temp;
    m_iovec[0].iov_len = response_head.size() * sizeof(char);

    bytes_write = writev(m_socket, m_iovec, 2);

    if (bytes_write <= 0)
    {
        return false;
    }
    unmap();
    if (m_map["Connection"] == "keep-alive")
    {
        //return true;
        return false;
    }
    else
    {
        return false;
    }
}
