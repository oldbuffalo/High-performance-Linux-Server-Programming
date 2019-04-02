#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/time.h>
#include<errno.h>


//分别展示epoll的ET和LT模式
/*
LT: epoll_wait检测到其上有事件发生并将此事件通知给应用程序，应用程序可以不立即处理该事件
    这样，应用程序下一次调用epoll_wait时，epoll_wait还会再次向应用程序通告此事件，直到
	事件被处理
ET: 当epoll_wait检测到其上有事件发生并将此事件通知应用程序后，应用程序必须立即处理该事件
    因为后续的epoll_wait调用将不再向应用程序通知这一事件
ET减少了同一个epoll事件被重复触发的次数  因此效率比LT要高
*/

typedef struct sockaddr SA;
#define BACKLOG 128
#define MAX_EVENT_SIZE 1024
#define BUF_SIZE 10

//将文件描述符设置为非阻塞
int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

//将文件描述符fd上的EPOLLIN注册到epollfd指示的epoll内核事件表
//参数enable_et指定是否对fd启用ET模式
void addfd(int epollfd,int fd,bool enable_et){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN;
	if(enable_et)
		event.events |= EPOLLET;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnoblocking(fd);  //把每个fd都设置成非阻塞的
}

//lt工作模式  接受数据
void lt(epoll_event* events,int number,int epollfd,int listenfd){
	char buf[BUF_SIZE];
	memset(buf,'\0',BUF_SIZE);
	//events数组中都是就绪事件
	for(int i = 0;i<number;i++){
		int sockfd = events[i].data.fd;
		if(sockfd == listenfd){
			//有新的连接
			struct sockaddr_in clientaddr;
			socklen_t nlen = sizeof(clientaddr);
			int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
			assert(confd != -1);
			printf("new client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
			addfd(epollfd,confd,false);
		}
		else if(events[i].events & EPOLLIN){
			//只要socket读缓存中还有未读出的数据，这段代码就被触发
			printf("lt:event trigger once\n");
			memset(buf,'\0',BUF_SIZE);
			int ret = recv(sockfd,buf,BUF_SIZE-1,0);
			if(ret <= 0){
				close(sockfd);
				continue;
			}
			printf("get %d bytes of content:%s\n",ret,buf);
		}
		else
			printf("something else happened\n");

	}
}

void et(epoll_event* events,int number,int epollfd,int listenfd){
	char buf[BUF_SIZE];
	memset(buf,'\0',BUF_SIZE);
	//events数组中都是就绪事件
	for(int i = 0;i<number;i++){
		int sockfd = events[i].data.fd;
		if(sockfd == listenfd){
			//有新的连接
			struct sockaddr_in clientaddr;
			socklen_t nlen = sizeof(clientaddr);
			int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
			assert(confd != -1);
			printf("new client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
			addfd(epollfd,confd,true);
		}
		else if(events[i].events & EPOLLIN){
			//这段代码不会被重复触发，所以要循环读取数据，以确保socket读缓存中的所有数据被读出
			printf("et:event trigger once\n");
			while(1)
			{
				memset(buf,'\0',BUF_SIZE);
				int ret = recv(sockfd,buf,BUF_SIZE-1,0);
				if(ret < 0){
					//对于非阻塞IO 下面的条件成立表示数据已经全部读取完毕
					//此后，epoll能再次触发sockfd上的EPOLLIN事件，以驱动下一次读操作
					if(errno == EAGAIN || errno == EWOULDBLOCK){
						printf("read later\n");
						break;
					}
					close(sockfd);
					break;
				}
				if(ret == 0)
					close(sockfd);
				else
					printf("get %d bytes of content:%s\n",ret,buf);
			}

		}
		else
			printf("something else happened\n");
	}
}


int main(int argc,char* argv[])
{
	//传入ip，port
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}
	const char* ip =argv[1];
	int port = atoi(argv[2]);

	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int ret = bind(listenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);
	
	ret = listen(listenfd,BACKLOG);
	assert(ret != -1);

	struct sockaddr_in clientaddr;
	socklen_t nlen = sizeof(clientaddr);
	
	epoll_event events[MAX_EVENT_SIZE];  

	int epollfd = epoll_create(5);  //参数没作用 大于0就行
	assert(epollfd != -1);
	
	//将监听socket注册到内核事件表  并且开启ET
	addfd(epollfd,listenfd,true);

	while(1){
		int ret = epoll_wait(epollfd,events,MAX_EVENT_SIZE,-1);  //阻塞调用
		if(ret < 0){
			printf("epoll call fail\n");
			break;
		}
		//lt(events,ret,epollfd,listenfd);
		et(events,ret,epollfd,listenfd);
		
	}

	
	close(listenfd);
	return 0;
}
