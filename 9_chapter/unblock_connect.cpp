#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/select.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/time.h>
#include<errno.h>


#define BUF_SIZE 1023

typedef struct sockaddr SA;

//将文件描述符设置为非阻塞
int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

//超时连接函数  参数分别为服务器ip 端口号 超时时间(秒)
int unblock_connect(const char* ip,int port,int time){
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);
	
	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(socket >= 0);
	int fdopt = setnoblocking(sockfd);
	
	int ret = connect(sockfd,(SA*)&serveraddr,sizeof(serveraddr));
	/*
	对于非阻塞的socket，在对非阻塞的socket调用connect,而连接没有立即建立时
	可以调用select poll等函数监听这个连接失败的socket上的可写事件.当select
	poll等函数返回后,再利用getsockopt来读取错误码并清楚该socket上的错误.如果
	错误码是0,表示连接成功建立,否则连接失败.
	*/
	if(ret == 0){
		//连接成功建立  就回复sockfd的属性 并立即返回
		printf("connect with server immediately\n");
		fcntl(sockfd,F_SETFL,fdopt);
		return sockfd;
	}
	else if(errno != EINPROGRESS){
		//如果连接没有立即建立 那么只有当错误码是EINPROGRESS才表示连接还在进行,否则出错返回
		printf("unblock connect not support\n");
		return -1;
	}

	fd_set readfds;
	fd_set writefds;
	FD_ZERO(&readfds);
	FD_SET(sockfd,&writefds);  //监听sockfd的可写事件
	struct timeval timeout;
	timeout.tv_sec = time; //秒
	timeout.tv_usec = 0;     //微秒
	
	ret = select(sockfd+1,&readfds,&writefds,NULL,&timeout);
	if(ret <= 0){
		//select超时或者出错,立即返回
		printf("connectiom time out\n");
		close(sockfd);
		return -1;
	}
	
	if(!FD_ISSET(sockfd,&writefds)){
		//sockfd上没有可写事件
		printf("no events on sockfd found\n");
		close(sockfd);
		return -1;
	}

	int error = 0;
	socklen_t nlen = sizeof(error);
	//调用getsockopt获取并清除sockfd上的错误
	if(getsockopt(sockfd,SOL_SOCKET,SO_ERROR,&error,&nlen) < 0){
		printf("get socket option failed\n");
		close(sockfd);
		return -1;
	}
	//错误号不为0 表示连接出错
	if(error != 0){
		printf("connection failed after select with the error:%d\n",error);
		close(sockfd);
		return -1;
	}
	//连接成功
	printf("connection ready after select with the socket:%d\n",sockfd);
	fcntl(sockfd,F_SETFL,fdopt);

	return sockfd;
}

int main(int argc,char* argv[])
{
	//传入服务器ip，port
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}
	const char* ip =argv[1];
	int port = atoi(argv[2]);
	
	int sockfd = unblock_connect(ip,port,10);
	if(sockfd < 0)
		exit(-1);
	
	close(sockfd);
	
	return 0;
}
