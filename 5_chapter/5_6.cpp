#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<assert.h>
#include<errno.h>
#include<unistd.h>
#include<stdlib.h>
#include<arpa/inet.h>


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
	
	int ret = connect(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	const char* oob_data = "abc"; //带外数据
	const char* normal_data = "123";
	send(sockfd,normal_data,strlen(normal_data),0);
	send(sockfd,oob_data,strlen(oob_data),MSG_OOB);
	send(sockfd,normal_data,strlen(normal_data),0);


	close(sockfd);
	return 0;
}
