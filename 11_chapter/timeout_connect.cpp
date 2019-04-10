#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<assert.h>
#include<errno.h>
typedef struct sockaddr SA;

int timeout_connect(const char* ip,int port,int time){

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(socket >= 0);

	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);
	
	//通过SO_RCVTIMEO和SO_SNDTIMEO所设置的超时时间类型是timeval,这和select系统调用的超时参数类型一样
	struct timeval timeout;
	timeout.tv_sec = time;
	timeout.tv_usec = 0;
	socklen_t nlen = sizeof(timeout);

	int ret = setsockopt(sockfd,SOL_SOCKET,SO_SNDTIMEO,&timeout,nlen);
	assert(ret != -1);
	
	ret = connect(sockfd,(SA*)&serveraddr,sizeof(serveraddr));

	//超时返回-1 并且errno为EINPROGRESS
	if(ret == -1){
		if(errno = EINPROGRESS){
			//处理超时事件
			printf("connect timeout,process timeout logic\n");
			return -1;
		}
		printf("error occur when connecting to server\n");
		return -1;
	}

	return sockfd;
}

int main(int argc,char* argv[])
{
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	int sockfd = timeout_connect(ip,port,10);
	printf("sockfd:%d\n",sockfd);
	if(sockfd < 0)
		return -1;

	return 0;
}
