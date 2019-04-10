#define _GUN_SOURCE 1  //POLLRDHUP需要
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
#include<errno.h>


typedef struct sockaddr SA;
#define BUFSIZE 1024
#define BACKLOG 128
#define USER_LIMIT  5    //最大用户数量限制
#define FD_LIMIT 65535 //文件描述符最大限制

/*
聊天室服务器程序  管理监听socket和连接socket  通过I/O复用实现 用poll来监听
作用:
1.接收客户数据,并把客户数据发送给每一个登录到该服务器上的客户端(数据发送者除外)
*/

struct client_data{
	sockaddr_in address; //客户端地址
	char* write_buf;   //待写到客户端的数据的位置
	char buf[BUFSIZE]; //从客户端读入的数据
};

int setnonblocking(int fd){
	int oldoption = fcntl(fd,F_GETFL);
	int newoption = oldoption | O_NONBLOCK;
	fcntl(fd,F_SETFL,newoption);
	return oldoption;
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

	int reuse = 1;
	int ret = setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
	assert(ret != -1);

	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip ,&serveraddr.sin_addr);
	ret = bind(listenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	ret = listen(listenfd,BACKLOG);
	assert(ret != -1);
	

	//为了提高poll的性能 限制了用户的数量
	struct pollfd fds[ USER_LIMIT + 1];  //用户连接socket总数+listenfd
	fds[0].fd = listenfd;
	fds[0].events = POLLIN | POLLERR;
	fds[0].revents = 0;
	for(int i = 1;i< USER_LIMIT;i++){
		fds[i].fd = -1;
		fds[i].events = 0;
		fds[i].revents = 0;
	}
	
	char buf[BUFSIZE];
	int user_count = 0; //标识当前用户连接人数

	//创建users数组 分配FD_LIMIT个client_data对象
	//socket的值可以直接用来做索引对应的client_data
	//将socket和客户数据关联起来的简单而高效的方式
	client_data* users = new client_data[FD_LIMIT];
	while(1){
		ret = poll(fds,user_count+1,-1);
		if(ret < 0){
			printf("poll call fail\n");
			break;
		}

		//poll的缺陷  需要遍历整个pollfd数组才能找出就绪的fd
		for(int i = 0;i<user_count+1;i++){
			if(fds[i].fd == listenfd  && fds[i].revents & POLLIN){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
				if(confd < 0){
					printf("errno is:%d\n",errno);
					continue;
				}
				//如果请求过多  关闭新的连接
				if(user_count >= USER_LIMIT){
					const char* info = "too many users\n";
					printf("%s\n",info);
					send(confd,info,strlen(info),0);
					close(confd);
					continue;
				} 
				//接收到新连接
				users[confd].address = clientaddr;
				setnonblocking(confd);
				user_count++;
				fds[user_count].fd = confd;
				fds[user_count].events = POLLIN | POLLRDHUP | POLLERR;
				printf("new connection ip:%s port:%d fd:%d,now have %d users\n",
				       inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port),
					   confd,user_count);
			}
			else if(fds[i].revents & POLLERR){
				printf("got an error from %d\n",fds[i].fd);
				char errors[100];
				memset(errors,'\0',100);
				socklen_t nlen = sizeof(errors);
				if(getsockopt(fds[i].fd,SOL_SOCKET,SO_ERROR,&errors,&nlen) < 0)
					printf("get socket option fail\n");
				continue;
			}
			else if(fds[i].revents & POLLRDHUP){
				//如果客户端关闭了  服务器也关闭对应的连接,并将用户总数减1
				users[fds[i].fd] = users[fds[user_count].fd];
				close(fds[i].fd);
				fds[i].fd = -1;
				bzero(&fds[i].revents,sizeof(fds[i].revents));// 很关键
				//拿最后一个填入空位 
				//因为user_count减1了,所以填入前面通过减i检测这上面的事件
				//下次新的连接来 分配的是可用的最小的fd,因此这个被填入的fd
				//又被新的连接覆盖,最后的通过user_count加1又能访问到
				fds[i] = fds[user_count];  
				i--; // 保证能遍历到填入空位的fd
				user_count--;
				printf("a client left\n");
			}
			else if(fds[i].revents & POLLIN){
				int confd = fds[i].fd;
				memset(users[confd].buf,'\0',BUFSIZE);
				ret = recv(confd,users[confd].buf,BUFSIZE-1,0);
				printf("get %d bytes of client data from %d,data is %s",
				       ret,confd,users[confd].buf); // 服务器能接收到
				//非阻塞读取
				if(ret < 0){
					if(errno != EAGAIN){
						//真的出错了
						close(confd);
						users[fds[i].fd] = users[fds[user_count].fd];
						fds[i] = fds[user_count];
						i--;
						user_count--;
					}
				}
				else if(ret > 0){
					//接收到客户数据 通知其他socket准备写数据
					for(int j = 1;j<=user_count;j++){
						int sockfd = fds[j].fd;  //标识除了发送数据之外的别的fd
						if(sockfd == confd)
							continue;
						fds[j].events |= ~POLLIN; // ??
						fds[j].events |= POLLOUT; //??
						users[sockfd].write_buf = users[confd].buf;
				//		send(sockfd,users[sockfd].write_buf,strlen(users[sockfd].write_buf),0);
					}
				}
			}
			else if(fds[i].revents & POLLOUT){
				int confd = fds[i].fd;
				if(!users[confd].write_buf)
					continue;
				ret = send(confd,users[confd].write_buf,strlen(users[confd].write_buf),0);
				users[confd].write_buf = NULL;
				fds[i].events |= ~POLLOUT;
				fds[i].events |= POLLIN;
			}
		}
	}

	
	delete []users;
	close(listenfd);
	return 0;
}
