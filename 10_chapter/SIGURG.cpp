#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<assert.h>
#include<errno.h>
#include<signal.h>
#include<string.h>
#include<fcntl.h>
/*
用SIGURG --->内核通知应用程序带外数据到达
还有一种方式 就是 IO复用检测异常事件
*/

/*
此程序在Ubuntu 16.04有问题
接收不到带外数据
*/
typedef struct sockaddr SA;
#define BUF_SIZE 1024
static int confd;

void sig_urg(int sig){
	int save_errno = errno;  //在信号处理函数中可能产生一些错误  但是不能影响主线程的逻辑
	printf("--------\n");
	char buf[BUF_SIZE];
	memset(buf,'\0',BUF_SIZE);
	int ret = recv(confd,buf,BUF_SIZE-1,MSG_OOB); //接收带外数据
	assert(ret >= 0);
	printf("got %d bytes of oob data:%s\n",ret,buf);
	errno = save_errno;
}

void addsig(int sig){
	struct sigaction sa;
	sa.sa_handler = sig_urg;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	int ret = sigaction(sig,&sa,NULL);
	assert(ret != -1);
}


int main(int argc,char* argv[])
{
	//传入ip port
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);


	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);
	
	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);

	int ret = bind(listenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	ret = listen(listenfd,1);
	assert(ret != -1);

	struct sockaddr_in clientaddr;
	socklen_t nlen = sizeof(clientaddr);

	confd = accept(listenfd,(SA*)&clientaddr,&nlen);
	assert(confd >= 0);
	printf("new connection ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));

	//设置信号处理函数
	addsig(SIGURG);

	//在使用SIGURG信号之前  必须设置socket的宿主进程或进程组
	fcntl(confd,F_SETOWN,getpid());
	
	char buf[BUF_SIZE];
	while(1){
		//循环接受普通数据
		memset(buf,'\0',BUF_SIZE);
		ret = recv(confd,buf,BUF_SIZE-1,0);
		if(ret <= 0)
			break;
		printf("got %d bytes of normal data :%s\n",ret,buf);
	}

	close(confd);
	close(listenfd);
	return 0;
}
