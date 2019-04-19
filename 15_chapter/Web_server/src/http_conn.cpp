#include"http_conn.h"

//定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permisson to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//网站的根目录
const char* doc_root = "/home/colin/www/html";

int setnonblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

void addfd(int epollfd,int fd,bool one_shot){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if(one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnonblocking(fd);
}

void removefd(int epollfd,int fd){
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
	close(fd);
}

void modfd(int epollfd,int fd,int ev){
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
	epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void http_conn::close_conn(bool real_close){
	//real_close 缺省值为true
	if(real_close && m_sockfd != -1){
		removefd(m_epollfd,m_sockfd);  //close操作在这里
		m_sockfd = -1;
		m_user_count--;
	}
}

//在主线程成功接受一个新的连接的时候被调用
void http_conn::init(int sockfd,const sockaddr_in &addr){
	m_sockfd = sockfd;
	m_addr = addr;
	//以下两行是为了避免TIME_WAIT状态 仅用于调试  实际使用时应该去掉
	int reuse = 1;
	setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
	addfd(m_epollfd,sockfd,true);
	m_user_count++;
	init();
}

void http_conn::init(){
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = false;
	m_method = GET;
	m_url = NULL;
	m_version = NULL;
	m_host = NULL;
	m_content_length = 0;
	m_start_line = 0;
	m_checkd_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;
	memset(m_read_buf,'\0',READ_BUFFER_SIZE);
	memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
	memset(m_real_file,'\0',FILENAME_LEN);
}

//从状态机   分析一个行的状态  被process_read()调用
LINE_STATUS http_conn::parse_line(){
	char temp;
	while(m_checkd_idx < m_read_idx){
		temp = m_read_buf[m_checkd_idx];
		if(temp == '\r'){
			if(m_checkd_idx+1 == m_read_idx) //代表不是完整的一行
				return LINE_OPEN;
			else if(m_read_buf[m_checkd_idx+1] == '\n'){
				m_read_buf[m_checkd_idx++] = '\0';
				m_read_buf[m_checkd_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if(temp == '\n'){
			if(m_checkd_idx > 1 && m_read_buf[m_checkd_idx-1] == '\r'){
				m_read_buf[m_checkd_idx-1] = '\0';
				m_read_buf[m_checkd_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}

		m_checkd_idx++;
	}
	
	return LINE_OPEN;
}


//循环读取客户数据  直到无数据可读或者对方关闭连接
//在main函数中 EPOLLIN事件触发时  调用
bool http_conn::read(){
	if(m_read_idx >= READ_BUFFER_SIZE)
		return false;
	
	int bytes_read = 0;
	while(1){
		bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
		if(bytes_read == -1){
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return false;
		}
		else if(bytes_read == 0)
			return false;
		m_read_idx += bytes_read;
	}
	
	return true;

}

//解析HTTP请求行，获得请求方法，url，HTTP版本
//GET url HTTP/1.1
//分析HTTP请求的请求行  在process_read()中CHECK_STATE_REQUESTLINE状态下被调用
HTTP_CODE http_conn::parse_request_line(char* text){
	printf("parse request_line,%s\n",text);
	m_url = strpbrk(text," \t"); //遇到空格停止返回第一个空格的地址
	if(!m_url)
		return BAD_REQUEST;
	*m_url++ = '\0'; 

	char* method = text;
	if(strcasecmp(method,"GET") == 0)
		m_method = GET;
	else
		return BAD_REQUEST;
	
	m_url += strspn(m_url," \t");  //去除空格
	m_version = strpbrk(m_url," \t");
	if(!m_version)
		return BAD_REQUEST;
	*m_version++ = '\0';
	m_version += strspn(m_version," \t");
	if(strcasecmp(m_version,"HTTP/1.1") != 0)
		return BAD_REQUEST;
	if(strncasecmp(m_url,"http://",7) == 0){
		m_url += 7;
		m_url = strchr(m_url,'/');  //从后往前找第一个/
	}
	if(!m_url || m_url[0] != '/')
		return BAD_REQUEST;
	printf("url:%s\n",m_url);	
	m_check_state = CHECK_STATE_HEADER;

	return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
//在process_read()CHECK_STATE_HEADER状态下被调用
HTTP_CODE http_conn::parse_headers(char* text){
	printf("parse headers,%s\n",text);
	//遇到空行 表示头部字段解析完毕
	if(text[0] == '\0'){
		//如果HTTP请求有消息体  则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT
		if(m_content_length != 0){
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		//否则说明已经得到一个完整的HTTP请求
		return GET_REQUEST;
	}
	else if(strncasecmp(text,"Connection:",11) == 0){
		text += 11;
		text += strspn(text," \t");
		if(strcasecmp(text,"keep-alive") == 0)
			m_linger = true;
		printf("%s\n",text);

	}
	else if(strncasecmp(text,"Content-Length:",15) == 0){
		text += 15;
		text += strspn(text," \t");
		m_content_length = atol(text);
		printf("%s\n",text);
	}
	else if(strncasecmp(text,"Host:",5) == 0){
		text += 5;
		text += strspn(text," \t");
		m_host = text;
		printf("%s\n",text);
	}
	else
		printf("unknow header %s\n",text);
	
	return NO_REQUEST;
}

//这里没有真正解析HTTP请求的消息体  只是判断它是否被完整地读入
//在process_read() CHECK_STATE_CONTENT状态下被调用
HTTP_CODE http_conn::parse_content(char* text){
	if(m_read_idx >= (m_checkd_idx+m_content_length)){
		printf("parse content\n");
		text[m_content_length] = '\0';
		return GET_REQUEST;
	}
	return NO_REQUEST;
}


//主状态机  在process中被调用
HTTP_CODE http_conn::process_read(){
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char* text = NULL;
	while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
		  || (line_status = parse_line()) == LINE_OK){
		text = get_line();
		m_start_line = m_checkd_idx;
	//	printf("got 1 http line:%s\n",text);
		
		switch(m_check_state){
			case CHECK_STATE_REQUESTLINE:
				ret = parse_request_line(text);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				break;
			case CHECK_STATE_HEADER:
				ret = parse_headers(text);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				else if(ret == GET_REQUEST)
					return GET_REQUEST;
				break;
			case CHECK_STATE_CONTENT:
				printf("-----------------\n");
				ret = parse_content(text);
				if(ret == GET_REQUEST)
					return do_request();
				line_status = LINE_OPEN;
				break;
			default:
				return INTERNAL_ERROR;
		}
	}
	
	return NO_REQUEST;
}

//当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性。如果目标文件存在，对所有用户可读，且
//不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
//在process_read()函数中的CHECK_STATE_CONTENT  如果拿到的是一个完整的HTTP请求 就调用do_request
HTTP_CODE http_conn::do_request(){
	//m_real_file 客户请求目标文件的完整路径 doc_root+url
	printf("into do_request\n");
	strcpy(m_real_file,doc_root);
	int len = strlen(doc_root);
	strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
	printf("m_real_file:%s\n",m_real_file);
	//m_file_stat 是类中一个远程  struct stat类型  目标文件的属性
	if(stat(m_real_file,&m_file_stat) < 0){
		printf("do_request no_resource\n");
		return NO_RESOURCE;
	}
	if(!(m_file_stat.st_mode & S_IROTH)){
		printf("do_request forbidden_request\n");
		return FORBIDDEN_REQUEST;
	}
	if(S_ISDIR(m_file_stat.st_mode)){
		printf("do_request bad_request\n");
		return BAD_REQUEST;
	}
	printf("to open requested file\n");
	int fd = open(m_real_file,O_RDONLY);
	m_file_address = (char*)mmap(NULL,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
	close(fd);

	return FILE_REQUEST;
}

//对内存映射区执行munmap操作
//unmap在写HTTP响应发生错误的时候 调用    发送HTTP响应成功  也调用
void http_conn::unmap(){
	if(m_file_address){
		munmap(m_file_address,m_file_stat.st_size);
		m_file_address = NULL;
	}
}

//写HTTP响应   在main函数中 检测到EPOLLOUT事件  被调用
bool http_conn::write(){
	int bytes_have_send = 0;
	int bytes_to_send = m_write_idx;
	if(bytes_to_send == 0){
		modfd(m_epollfd,m_sockfd,EPOLLIN);  //?
		init();
		return true;
	}
	int temp = 0;
	while(1){
		temp = writev(m_sockfd,m_iv,m_iv_count);   //这里数据真正的发生
		if(temp <= -1){
			//如果TCP写缓冲区没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，服务器无法立即收到
			//同一个客户的下一个请求，但这可以保证连接的完整性
			if(errno == EAGAIN){
				modfd(m_epollfd,m_sockfd,EPOLLOUT); //?
				return true;
			}
			unmap();
			return false;
		}
		bytes_to_send -= temp;
		bytes_have_send += temp;
		if(bytes_to_send <= bytes_have_send){   //?
			//发送HTTP响应成功  根据HTTP请求中的Connection字段决定是否立即关闭连接
			unmap();
			if(m_linger){
				init();
				modfd(m_epollfd,m_sockfd,EPOLLIN);
				return true;
			}
			else{
				modfd(m_epollfd,m_sockfd,EPOLLIN);
				return false;
			}

		}

	}

}

//往写缓冲区中写入待发送的数据   实现可变参数   被接下来的添加响应的一系列函数调用
bool http_conn::add_response(const char* format,...){
	if(m_write_idx >= WRITE_BUFFER_SIZE)
		return false;
	va_list arg_list;
	va_start(arg_list,format);
	int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
	if(len >= (WRITE_BUFFER_SIZE-1-m_write_idx))
		return false;
	m_write_idx += len;
	va_end(arg_list);
	return true;
}

//状态行  HTTP/1.1 状态码 状态信息  被process_write调用
bool http_conn::add_status_line(int status,const char* title){
	return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

//响应头部信息  被process_write调用
bool http_conn::add_headers(int content_len){
	add_content_length(content_len);
	add_linger();
	add_blank_line();
}

//响应头部长度字段  被add_headers调用
bool http_conn::add_content_length(int content_len){
	return add_response("Content-Length: %d\r\n",content_len);
}

//响应头部 连接状态字段  被add_headers调用
bool http_conn::add_linger(){
	return add_response("Connection: %s\r\n",m_linger == true ? "keep-alive":"close");
}
//响应头部 终止的空白行 被add_headers调用
bool http_conn::add_blank_line(){
	return add_response("%s","\r\n");
}

//响应的消息体 被process_write调用
bool http_conn::add_content(const char* content){
	return add_response("%s",content);
}

bool http_conn::DealRet(int err,const char* title,const char* form){
	add_status_line(err,title);
	add_headers(strlen(form));
	if(!add_content(form))
		return false;
	return true;
}

//根据服务器处理HTTP请求的结果  决定返回给客户端的内容
//被process函数调用   等待process_read()返回的处理状态码
bool http_conn::process_write(HTTP_CODE ret){
	switch(ret){
		case INTERNAL_ERROR:
			if(!DealRet(500,error_500_title,error_500_form))
				return false;
			break;
		case BAD_REQUEST:
			if(!DealRet(400,error_400_title,error_400_form))
				return false;
			break;
		case NO_RESOURCE:
			if(!DealRet(404,error_404_title,error_404_form))
				return false;
			break;
		case FORBIDDEN_REQUEST:
			if(!DealRet(403,error_403_title,error_403_form))
				return false;
			break;
		case GET_REQUEST:{	
			add_status_line(200,ok_200_title);
			const char* ok_string = "<html><body>HHHHH</body></html>";
			add_headers(strlen(ok_string));
			if(!add_content(ok_string))
				return false;
			break;
		}
		case FILE_REQUEST:
			add_status_line(200,ok_200_title);
			if(m_file_stat.st_size != 0){
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				return true;
			}
			else{
				const char* ok_string = "<html><body></body></html>";
				add_headers(strlen(ok_string));
				if(!add_content(ok_string))
					return false;
			}
			break;
		default:
			return false;
	}
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	return true;
}

//由线程池中的工作线程调用  这是处理HTTP请求的入口函数
void http_conn::process(){
	HTTP_CODE read_ret = process_read();
	if(read_ret == NO_REQUEST){
		modfd(m_epollfd,m_sockfd,EPOLLIN);
		return;
	}
	printf("read_ret:%d\n",read_ret);
	bool write_ret = process_write(read_ret);
	if(!write_ret)
		close_conn();
	printf("response:\n%s\n",m_write_buf);
	printf("response data:%s\n",m_file_address);
	modfd(m_epollfd,m_sockfd,EPOLLOUT);
}


