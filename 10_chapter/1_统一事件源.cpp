#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<assert.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<errno.h>
#include<signal.h>
#include<fcntl.h>

/*
统一事件源  统一处理信号和I/O事件
信号处理函数需要尽可能快地执行完毕,以确保该信号不被屏蔽
解决方案:把信号的主要处理逻辑放到程序的主循环中,当信号处理函数触发的时候,它只是简单
地通知主循环程序接收到信号,并把信号值传递给主循环,主循环再根据接受到的信号值执行
目标信号对应的逻辑代码
信号处理函数通常使用管道来将信号传递给主循环
信号处理函数往管道的写端写入信号值,主循环则从管道的读端读出信号值
主循环怎么知道管道上何时有数据可以读? 使用I/O复用监听管道的读端文件描述符上的可读事件

这样  信号事件就能和别的I/O事件一样被处理  也就是统一事件源
*/

typedef struct sockaddr SA;

#define MAX_EVENT_SIZE 1024
static int pipefd[2];

int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnoblocking(fd);
}

void sig_handler(int signo){
	//保留原有的errno  在函数最后恢复  以保证函数的可重入性
	int save_errno = errno;
	int msg = signo;
	//将信号值写入管道  以通知主循环  
	printf("into sig_handler\n");
	send(pipefd[1],(char*)&msg,1,0);
	errno = save_errno;
}

//设置信号处理函数
void addsig(int signo){
	struct sigaction sa;
	memset(&sa,'\0',sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;  //重新调用被该信号终止的系统调用
	sigemptyset(&sa.sa_mask);
	//sigfillset(&sa.sa_mask);
	int ret = sigaction(signo,&sa,NULL);
	assert(ret != -1);
}

int main(int argc,char* argv[])
{
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip,&addr.sin_addr);

	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);
	
	int ret = bind(listenfd,(SA*)&addr,sizeof(addr));
	assert(ret != -1);
		
	ret = listen(listenfd,5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_SIZE];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);
	addfd(epollfd,listenfd);


	//使用socketpair创建管道  注册pipefd[0]上的可读事件 
	//第一个参数只能是AF_UNIX  因为只能在本地创建双向管道
	ret = socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
	assert(ret != -1);
	setnoblocking(pipefd[1]);
	addfd(epollfd,pipefd[0]);

	//设置一些信号的处理函数
	addsig(SIGHUP);
	addsig(SIGCHLD);
	addsig(SIGTERM);
	addsig(SIGINT);

	bool stop_server = false;

	//epoll管理监听socket  连接socket  和管道pipefd[0]的读事件
	while(!stop_server){
		int ready = epoll_wait(epollfd,events,MAX_EVENT_SIZE,-1);
		if(errno == EINTR)
			printf("stop by signal\n");
		if(ready < 0 && errno != EINTR){
			//EINTR错误表示程序在执行处于阻塞状态的系统调用事收到信号,该系统调用被中断  
			printf("epoll call fail\n");
			break;
		}
		//epoll返回的都是就绪事件
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == listenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
				printf("new connection ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
				addfd(epollfd,confd);
			}
			else if(sockfd ==  pipefd[0] && events[i].events & EPOLLIN){
				//从管道中读取信号值
				int signo;
				char signals[1024];
				ret = recv(pipefd[0],signals,sizeof(signals),0);
				if(ret  <= 0)
					continue;
				else{
					//因为每个信号值占1字节  所以按照字节来逐个接受信号
					for(int i = 0;i<ret;i++){
						switch(signals[i]){
							case SIGCHLD:
							case SIGHUP:
								continue;
							case SIGTERM:
							case SIGINT:
								stop_server = true;
						}

					}
				}
				
			}
			else if(events[i].events & EPOLLIN){
				//连接socket  接受发送数据
			}

		}

	}
	
	printf("close fds\n");
	close(listenfd);
	close(pipefd[0]);
	close(pipefd[1]);

	return 0;
}
