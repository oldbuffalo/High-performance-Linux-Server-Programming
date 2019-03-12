#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<assert.h>
#include<unistd.h>
#include<signal.h>
#include<stdlib.h>
#include<arpa/inet.h>

bool flag = true;


void handler(int n){
	flag = false;
	printf("bye\n");
}


int main(int argc,char* argv[])/*传入ip port backlog*/
{
	signal(SIGINT,handler);

	if(argc < 4 ){
		printf("usage:%s ip port backlog\n",argv[0]);
		return -1;
	}

	char* ip = argv[1];
	int port = atoi(argv[2]);
	int backlog = atoi(argv[3]);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd >= 0);

	struct sockaddr_in addr;
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);
	
	inet_ntop
	int ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	ret = listen(sockfd,backlog);
	assert(ret != -1);

	while(flag){
		sleep(1);
	}
	
	return 0;
}
