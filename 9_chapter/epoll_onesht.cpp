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

//EPOLLONESHOT事件
/*
在并发程序中，一个线程在读取完某个socket上的数据后开始处理这些数据，而在数据处理的
过程中该socket上又有新数据可读(EPOLLIN再次被触发)，此时另外一个线程被唤醒来读取这些
新的数据。于是出现了两个线程同时操作一个socket的情况，这不是我们期望的。
期望的是一个socket连接在任一时刻都只被一个线程处理
对于注册了EPOLLONESHOT事件的文件描述符，操作系统最多触发其上注册的一个可读、可写、异常事件
且只触发一次，除非我们使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件
因此注册了EPOLLONESHOT事件的socket一旦被某个线程处理完毕，该线程就应该立即重置这个socket
上的EPOLLONESHOT事件，以确保这个socket下一次可读时，其EPOLLIN事件能被触发
*/


typedef struct sockaddr SA;
#define BACKLOG 128
#define MAX_EVENT_SIZE 1024
#define BUF_SIZE 10

//传给线程的参数
struct fds{
	int epollfd;
	int sockfd;
};

//将文件描述符设置为非阻塞
int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

//将文件描述符fd上的EPOLLIN注册到epollfd指示的epoll内核事件表
//参数enable_oneshot指定是否对fd启用ET模式
void addfd(int epollfd,int fd,bool enable_oneshot){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;  
	if(enable_oneshot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnoblocking(fd);  //把每个fd都设置成非阻塞的
}

void reset_oneshot(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}

void* worker(void* arg){
	fds* pfds = (fds*)arg;
	int sockfd = pfds->sockfd;
	int epollfd = pfds->epollfd;
	printf("start new thread to receive data on fd:%d\n",sockfd);
	char buf[BUF_SIZE];
	memset(buf,'\0',BUF_SIZE);
	//循环读取非阻塞socket上的数据 直到出现EAGAIN错误
	while(1){
		/*
		如果一个工作线程处理完某个socket上的一次请求(处理过程用sleep描述),在数据处理
		结束之前又接收到该socket上新的客户请求，则该线程将继续为这个scoket服务，并且
		因为该socket上注册了EPOLLONESHOT事件，其他线程没有机会接触这个socket
		如果工作线程等待5s后仍然没有收到该socket上的下一批客户数据，则它将放弃为该
		socket服务。同时，调用reset_oneshot函数来重置该socket上的注册事件，使得epoll
		有机会再次检测到该socket上的EPOLLIN事件，进而使得其他线程有机会为该socket服务
		这样保证了同一时刻肯定只有一个线程为它服务，保证连接的完整性,避免了很多竞态条件
		*/
		int ret = recv(sockfd,buf,BUF_SIZE-1,0);
		if(ret == 0){
			close(sockfd);
			printf("remote client closed connection\n");
			break;
		}
		else if(ret < 0){
			if(errno == EAGAIN){
				//数据处理完毕之后  立即重置oneshot
				reset_oneshot(epollfd,sockfd);
				printf("read later\n");
				break;
			}
		}
		else{
			printf("get content:%s\n",buf);
			//休眠5s  模拟数据处理过程
			sleep(5);
		}

	}
	printf("end thread recviving data on fd:%d\n",sockfd);

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
	
	//将监听socket注册到内核事件表 listen socket不能注册EPOLLONESHOT事件
	//否则应用程序只能处理一个客户连接，因为后续的客户连接不再触发listenfd上的EPOLLIN事件
	addfd(epollfd,listenfd,false);

	while(1){
		int ret = epoll_wait(epollfd,events,MAX_EVENT_SIZE,-1);  //阻塞调用
		if(ret < 0){
			printf("epoll call fail\n");
			break;
		}
		
		for(int i = 0;i<ret;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == listenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
				printf("new client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
				//对于每个非监听socket都注册EPOLLONESHOT事件
				addfd(epollfd,confd,true);

			}
			else if(events[i].events & EPOLLIN){
				pthread_t pid;
				fds fds_for_new_worker;
				fds_for_new_worker.epollfd = epollfd;
				fds_for_new_worker.sockfd = sockfd;
				//启动一个工作线程位sockfd服务
				pthread_create(&pid,NULL,worker,(void*)&fds_for_new_worker);
			}
			else
				printf("something else happened\n");

		}
		
	}

	
	close(listenfd);
	return 0;
}
