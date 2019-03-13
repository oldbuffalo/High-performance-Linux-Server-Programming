#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<errno.h>
#include<assert.h>
#include<unistd.h>
#include<signal.h>
#include<stdlib.h>
#include<arpa/inet.h>

#define BUFFERSIZE 1024

int main(int argc,char* argv[])/*传入ip port backlog*/
{

	if(argc < 4 ){
		printf("usage:%s ip port recv_buf_size\n",argv[0]);
		return -1;
	}

	char* ip = argv[1];
	int port = atoi(argv[2]);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd >= 0);

	struct sockaddr_in addr;
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	/*注意在listen之前设置*/
	
	int recv_buf_size = atoi(argv[3]);
	int len= sizeof(recv_buf_size);
	setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,&recv_buf_size,sizeof(recv_buf_size));
	getsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,&recv_buf_size,(socklen_t*)&len);
	printf("recv buf size after setting is %d\n",recv_buf_size);


	int ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);
	ret = listen(sockfd,5);
	assert(ret != -1);

	
	struct sockaddr_in client;
	socklen_t nlen = sizeof(client);
	int confd = accept(sockfd,(struct sockaddr*)&client,&nlen);
	/*继承了监听socket的选项*/
	if(confd < 0)
		printf("errno is %d\n",errno);
	else{
		printf("connect ip:%s port:%d\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
		char buf[BUFFERSIZE];
		memset(buf,0,BUFFERSIZE);
		
		while(recv(confd,buf,BUFFERSIZE-1,0) > 0){}

		close(confd);
	}

	close(sockfd);
	return 0;
}
