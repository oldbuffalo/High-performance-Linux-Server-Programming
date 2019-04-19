#include<stdio.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>

#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

//因为有了任务类的存在  main函数就变得很简单  只负责I/O读写(调用的还是任务类的接口)

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);

void addsig(int sig,void(handler)(int),bool restart = true){
	struct sigaction sa;
	memset(&sa,'\0',sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig,&sa,NULL) != -1);
}

void show_error(int confd,const char* info){
	printf("%s\n",info);
	send(confd,info,strlen(info),0);
	close(confd);
}

int main(int argc,char* argv[])
{
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		return 1;
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	
	//忽略SIGPIPE信号
	addsig(SIGPIPE,SIG_IGN);

	//创建线程池
	threadpool<http_conn> *pool = NULL;
	try{
		pool = new threadpool<http_conn>;
	}
	catch(...){
		return 1;
	}
	
	//预先为每个可能的客户连接分配一个http_conn对象
	http_conn* users = new http_conn[MAX_FD];
	assert(users);

	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);

	//SO_LINGER选项用来控制close系统调用在关闭TCP连接时候的行为
	struct linger tmp = {1,0};
	
	//{1,0}这种情况下close立即返回 TCP模块丢弃被关闭的socket对应的发送缓冲区的残留数据
	//同时给对方发送一个复位报文段
	setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));


	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip,&addr.sin_addr);

	int ret = bind(listenfd,(struct sockaddr*)&addr,sizeof(sockaddr));
	assert(ret != -1);

	ret = listen(listenfd,5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);
	addfd(epollfd,listenfd,false);
	http_conn::m_epollfd = epollfd;

	while(1){
		int ready = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll fail\n");
			break;
		}

		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == listenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(struct sockaddr*)&clientaddr,&nlen);
				if(confd < 0){
					printf("accept errno is %d\n",errno);
					continue;
				}
				if(http_conn::m_user_count >= MAX_FD){
					show_error(confd,"Internal server busy");
					continue;
				}
				//初始化客户连接
				users[confd].init(confd,clientaddr);
			}
			else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
				//只要有一个事件发生就能进来  代表发生异常  直接关闭客户连接
				users[sockfd].close_conn();
			}
			else if(events[i].events &EPOLLIN){
				//根据读的结果  决定是将任务添加到线程池  然后关闭连接
				if(users[sockfd].read())
					pool->append(users+sockfd);
				else
					users[sockfd].close_conn();

			}
			else if(events[i].events & EPOLLOUT){
				//根据写的结果 决定是否关闭连接
				if(!users[sockfd].write())
					users[sockfd].close_conn();
			}

		}

	}

	close(epollfd);
	close(listenfd);
	delete []users;
	delete pool;
	return 0;
}
