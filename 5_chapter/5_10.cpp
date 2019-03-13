#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<assert.h>
#include<errno.h>
#include<unistd.h>
#include<stdlib.h>
#include<arpa/inet.h>

#define BUFFER_SIZE 512

int main(int argc,char* argv[])/*传入ip port backlog*/
{

	if(argc < 4 ){
		printf("usage:%s ip port send_buf_size\n",argv[0]);
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
	
	int send_buf_size = atoi(argv[3]);
	int nlen = sizeof(send_buf_size);
	setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,&send_buf_size,sizeof(send_buf_size));
	getsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,&send_buf_size,(socklen_t*)&nlen);
	printf("send buf size after setting is %d\n",send_buf_size);

	int ret = connect(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	char buf[BUFFER_SIZE];
	memset(buf,'a',BUFFER_SIZE);

	send(sockfd,buf,BUFFER_SIZE,0);

	close(sockfd);
	return 0;
}
