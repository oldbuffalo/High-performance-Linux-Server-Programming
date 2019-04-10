#define _GNU_SOURCE 1    //POLLRDHUP需要
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/poll.h>
#include<sys/time.h>
#include<fcntl.h>


typedef struct sockaddr SA;
#define BUFSIZE 1024

/*
聊天室客户端程序  同时处理网络连接和用户输入  通过I/O复用实现 用poll来监听
作用:
1.从标准输入终端读入数据,并将用户数据发送到服务器 利用splice函数直接将用户输入定向到socket
2.往标准输出打印服务器发送给它的数据
*/

int main(int argc,char* argv[])
{
	//传入ip，port
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}
	const char* ip =argv[1];
	int port = atoi(argv[2]);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd >= 0);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int ret = connect(sockfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	struct pollfd fds[2];
	fds[0].fd = 0; //标准输入
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = sockfd; //网络socket
	fds[1].events = POLLIN | POLLRDHUP | POLLERR;
	fds[1].revents = 0;
	
	int pipefd[2];   //读写
	ret = pipe(pipefd);
	assert(ret != -1);
	
	char buf[BUFSIZE];
	while(1){
		ret = poll(fds,2,-1);
		if(ret < 0){
			printf("poll call fail\n");
			break;
		}
		if(fds[1].revents & POLLERR){
			printf("got an error from %d\n",fds[1].fd);
			char errors[100];
			memset(errors,'\0',100);
			socklen_t nlen = sizeof(errors);
			if(getsockopt(fds[1].fd,SOL_SOCKET,SO_ERROR,&errors,&nlen) < 0)
			printf("get socket option fail\n");
		}

		if(fds[1].revents & POLLRDHUP){
			//对方关闭连接
			printf("server close the connection\n");
			break;
		}
		
		if(fds[1].revents & POLLIN){
			//接受数据
			memset(buf,'\0',sizeof(buf));
			ret = recv(sockfd,buf,BUFSIZE-1,0);
			if(ret <= 0){
				printf("recv call fail\n");
				break;
			}
			printf("%s\n",buf);
		}

		if(fds[0].revents & POLLIN){
			//标准输入触发
			splice(STDIN_FILENO,NULL,pipefd[1],NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
			splice(pipefd[0],NULL,sockfd,NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
		}

	}

	
	
	close(sockfd);
	return 0;
}
