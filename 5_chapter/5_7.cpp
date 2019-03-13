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

	if(argc < 3 ){
		printf("usage:%s ip port \n",argv[0]);
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
	
	int ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);
	ret = listen(sockfd,5);
	assert(ret != -1);

	struct sockaddr_in client;
	socklen_t nlen = sizeof(client);
	int confd = accept(sockfd,(struct sockaddr*)&client,&nlen);
	if(confd < 0)
		printf("errno is %d\n",errno);
	else{
		printf("connect ip:%s port:%d\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
		char buf[BUFFERSIZE];
		memset(buf,0,BUFFERSIZE);
		ret = recv(confd,buf,BUFFERSIZE-1,0);
		printf("recv %d bytes of normal data '%s' \n",ret,buf);
		
		memset(buf,0,BUFFERSIZE);
		ret = recv(confd,buf,BUFFERSIZE-1,MSG_OOB);
		printf("recv %d bytes of oob data '%s'\n",ret,buf);
		

		memset(buf,0,BUFFERSIZE);
		ret = recv(confd,buf,BUFFERSIZE-1,0);
		printf("recv %d bytes of normal data '%s' \n",ret,buf);
		
		close(confd);
	}

	close(sockfd);
	return 0;
}
