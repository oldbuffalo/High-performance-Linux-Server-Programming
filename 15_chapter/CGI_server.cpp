#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include"processpool.h"


/*用于处理客户CGI请求的类  它可以作为processpool类的模板参数*/
class cgi_con{
private:
	static const int BUF_SIZE = 1024;  //读缓冲区大小
	static int m_epollfd;  //某个子进程管理的内核事件表
	int m_sockfd;          //新建立连接socket
	sockaddr_in m_addr;    //新建立连接的客户端地址
	char m_buf[BUF_SIZE];
	int m_read_idx;   //标记读缓冲区已经读入的客户数据的最后一个字节的下一个位置
public:
	cgi_con(){}
	~cgi_con(){}
	//子进程 接收到新的连接时候  调用init
	void init(int epollfd,int sockfd,const sockaddr_in &client_addr){
		m_epollfd = epollfd;
		m_sockfd = sockfd;
		m_addr = client_addr;
		memset(m_buf,'\0',BUF_SIZE);
		m_read_idx = 0;
	}
	//子进程  接收到用户请求时候 调用process
	void process(){
		
		//循环读取和分析客户数据
		int idx = 0;
		int ret = 0;
		while(1){
			idx = m_read_idx;
			ret = recv(m_sockfd,m_buf+idx,BUF_SIZE-1-idx,0);
			if(ret < 0){
				if(errno != EAGAIN)
					removefd(m_epollfd,m_sockfd);
				break;
			}
			else if(ret == 0){
				removefd(m_epollfd,m_sockfd);
				break;
			}
			else{
				m_read_idx += ret;
				printf("user content is:%s\n",m_buf);
				//如果遇到\r\n  则开始处理客户请求
				while(idx<m_read_idx){
					if(idx >=1 && m_buf[idx-1] == '\r' && m_buf[idx] == '\n')
						break;
					idx++;
				}
				//如果没有遇到字符\r\n   则需要更多的客户数据
				if(idx == m_read_idx)
					continue;
				m_buf[idx-1]='\0';

				char* file_name = m_buf;
				//判断客户要运行的CGI程序是否存在
				if(access(file_name,F_OK) == -1){
					removefd(m_epollfd,m_sockfd);
					break;
				}
				//创建子进程来执行CGI程序
				ret = fork();
				if(ret < 0){
					removefd(m_epollfd,m_sockfd);
					break;
				}
				else if(ret > 0){
					//父进程只需关闭连接
					removefd(m_epollfd,m_sockfd);
					break;
				}
				else{
					//子进程将标准输出冲定向到m_sockfd，并执行CGI程序
					close(STDOUT_FILENO);
					dup(m_sockfd);
					execl(m_buf,m_buf,NULL);
					exit(0);	
				}

			}
		}//while

	}
};

int cgi_con:: m_epollfd = -1;

int main(int argc,char* argv[])
{
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		return 1;
	}
	printf("main pid:%d\n",getpid());

	const char* ip = argv[1];
	int port = atoi(argv[2]);


	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);

	int ret = 0;
	struct sockaddr_in addr;
	bzero(&addr,sizeof(addr));
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	inet_aton(ip,&addr.sin_addr);
	
	ret = bind(listenfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	ret = listen(listenfd,5);
	assert(ret != -1);
	
	processpool<cgi_con>* pool = processpool<cgi_con>::create(listenfd);
	if(pool){
		pool->run();
		delete pool;
	}
	close(listenfd);
	return 0;
}
