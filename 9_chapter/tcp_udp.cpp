#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/time.h>
#include<errno.h>

/*
不少服务器程序能同时监听多个端口  
一个socket只能与一个scoket地址绑定,也就是一个socket只能监听一个端口
服务器如果要监听多个端口 就必须监听多个socket,并将他们分别绑定个各个端口,
这样服务器需要管理多个socket,I/O复用就有用处
即使是同一个端口  如果服务器要同时处理该端口上的TCP和UDP请求,则也需要创建
两个不同的socket:一个是流socket,一个是数据报socket,并将它们绑定到同一个端口
*/

typedef struct sockaddr SA;
#define BACKLOG 128
#define MAX_EVENT_NUMBER 1024
#define TCP_BUF_SIZE 512
#define UDP_BUF_SIZE 1024


//将文件描述符设置为非阻塞
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
	setnoblocking(fd);  //把每个fd都设置成非阻塞的
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

	//创建TCP socket 
	int Tcplistenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(Tcplistenfd >= 0);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int ret = bind(Tcplistenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);
	
	ret = listen(Tcplistenfd,BACKLOG);
	assert(ret != -1);

	//创建UDP socket 
	bzero(&serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	inet_aton(ip,&serveraddr.sin_addr);
	serveraddr.sin_port = htons(port);

	int udpfd = socket(AF_INET,SOCK_DGRAM,0);
	assert(udpfd >= 0);

	ret = bind(udpfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUMBER];  
	int epollfd = epoll_create(5);  //参数没作用 大于0就行
	assert(epollfd != -1);
	//注册tcp socket 和 udp socket上的可读事件
	addfd(epollfd,Tcplistenfd);
	addfd(epollfd,udpfd);

	while(1){
		int ready = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);  //阻塞调用
		if(ready < 0){
			printf("epoll call fail\n");
			break;
		}
		
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == Tcplistenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(Tcplistenfd,(SA*)&clientaddr,&nlen);
				printf("new client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
				addfd(epollfd,confd);
			}
			else if(sockfd == udpfd){
				char buf[UDP_BUF_SIZE];
				memset(buf,'\0',UDP_BUF_SIZE);
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				ret = recvfrom(udpfd,buf,UDP_BUF_SIZE-1,0,(SA*)&clientaddr,&nlen);
				if(ret > 0){
					//echo服务
					sendto(udpfd,buf,ret,0,(SA*)&clientaddr,nlen);
				}
			}
			else if(events[i].events & EPOLLIN){
				char buf[TCP_BUF_SIZE];
				while(1){
					memset(buf,'\0',TCP_BUF_SIZE);
					ret = recv(sockfd,buf,TCP_BUF_SIZE-1,0);
					if(ret <0){
						if(errno == EAGAIN || errno == EWOULDBLOCK){
							break;
						}
						//如果不是上面的两种错误  表示真的发生错误
						close(sockfd);
						break;
					}
					else if(ret == 0){
						//对方关闭连接
						close(sockfd);
						break;
					}
					else
						send(sockfd,buf,ret,0);

				}
			}
			else
				printf("something else happened\n");

		}
		
	}

	
	close(Tcplistenfd);
	close(udpfd);
	return 0;
}
