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

int main(int argc,char** argv)
{
	//传入ip port 和 filename  因此参数是4个
	if(argc <= 3){
		printf("usage:%s ip port filename\n",argv[0]);
		exit(-1);
	}

	char* ip = argv[1];
	int port = atoi(argv[2]);
	char* filename = argv[3];

	int fd = open(filename,O_RDONLY);
	assert(fd != -1);
	struct stat file_stat;
	int ret =fstat(fd,&file_stat);
	assert(ret != -1);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd != -1);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	ret = bind(sockfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);


	ret = listen(sockfd,backlog);
	assert(ret != -1);

	struct sockaddr_in clientaddr;
	socklen_t nlen = sizeof(clientaddr);

	int confd = accept(sockfd,(SA*)&clientaddr,&nlen);
	if(confd  > 0){
		printf("client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));

		//第一个参数out_fd  代表往哪里写   必须是一个socket
		//第二个参数in_fd   代表从哪里读   必须指向真实的文件  
		//第三个参数       代表从哪里开始读 NULL默认起始位置
		sendfile(confd,fd,NULL,file_stat.st_size);
		
		close(confd);
	}
	else{
		printf("accept error\n");
	}
	return 0;
}
