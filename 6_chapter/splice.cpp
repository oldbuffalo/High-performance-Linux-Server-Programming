#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/sendfile.h>
typedef struct sockaddr SA;

#define backlog 1024

//splice函数用于在两个文件描述符之间移动数据

int main(int argc,char** argv)
{
	//传入ip port  因此参数是3个
	if(argc <= 2){
		printf("usage:%s ip port \n",argv[0]);
		exit(-1);
	}

	char* ip = argv[1];
	int port = atoi(argv[2]);


	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd != -1);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int ret = bind(sockfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);


	ret = listen(sockfd,backlog);
	assert(ret != -1);

	struct sockaddr_in clientaddr;
	socklen_t nlen = sizeof(clientaddr);

	int confd = accept(sockfd,(SA*)&clientaddr,&nlen);
	if(confd  > 0){
		printf("client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
		int pipe_fd[2];
		ret = pipe(pipe_fd);  //管道做数据的中转工作
		ret = splice(confd,NULL,pipe_fd[1],NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
		assert(ret != -1);
		ret = splice(pipe_fd[0],NULL,confd,NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
		assert(ret != -1);
		close(confd);
	}
	else{
		printf("accept error\n");
	}
	return 0;
}
