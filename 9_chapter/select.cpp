#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/select.h>
#include<sys/time.h>

/*
该demon演示select监听的socket接受普通数据和带外数据
对于普通数据 socket处于可读状态
对于异常数据 socket处于异常状态
*/

typedef struct sockaddr SA;
#define BACKLOG 128


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

	int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
	assert(confd>=0);
	printf("client connect ip:%s  port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
	char buf[1024];
	fd_set read_fds;
	fd_set exception_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&exception_fds);
	
	while(1){
		memset(buf,'\0',sizeof(buf));
		//注意 每次调用select都要重新设置read_fds和exception_fds

		//这个demon比较简单  只有一个处理数据的socket 
		//如果有多个 可以用专门的一个fd_set保存要监听的文件描述符
		FD_SET(confd,&read_fds);  
		FD_SET(confd,&exception_fds);
			
		ret = select(confd+1,&read_fds,NULL,&exception_fds,NULL); //阻塞
		if(ret < 0){
			printf("select call fail\n");
			break;
		}
		//select返回就绪的个数  这里每次都只有一个就绪事件  也就不要循环了
		//对于可读事件  用普通的recv读取数据
		if(FD_ISSET(confd,&read_fds)){
			ret = recv(confd,buf,sizeof(buf)-1,0);
			if(ret <= 0)
				break;
			printf("get %d bytes of normal data:%s\n",ret,buf);
		}

		//对于异常事件  采用带MSG_OOB标志的recv函数读取带外数据
		if(FD_ISSET(confd,&exception_fds)){
			ret = recv(confd,buf,sizeof(buf)-1,MSG_OOB);
			if(ret <= 0)
				break;
			printf("get %d bytes of oob data:%s\n",ret,buf);
		}
		
	}

	
	close(confd);
	close(listenfd);
	return 0;
}
