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
typedef struct sockaddr SA;

#define backlog 1024
#define BUF_SIZE 1024
/*
利用  writev  将两块分散的内存一次性发送出去
在发送http回复的时候  回复头  和  真正回复的文件  一般放在两块不连续的内存中

1.一种方法 将回复头和文件分别读入内存  然后拼接  再一起发送
2.另一种方法  利用writev直接集中写
*/
static const char* HTTP_ACK[2] ={"200 OK","400 error"}; 

int main(int argc,char** argv)
{
	//传入ip 和 port 文件名  因此参数是4个
	if(argc <= 3){
		printf("usage:%s ip port filename\n",argv[0]);
		exit(-1);
	}

	char* ip = argv[1];
	int port = atoi(argv[2]);
	char* filename = argv[3];

	//直接传入解析之后的文件名，省去了请求头的解析

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd != -1);
	
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int reuse = 1;
	setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

	int ret = bind(sockfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);


	ret = listen(sockfd,backlog);
	assert(ret != -1);

	struct sockaddr_in clientaddr;
	socklen_t nlen = sizeof(clientaddr);
	
	int confd = accept(sockfd,(SA*)&clientaddr,&nlen);
	if(confd  > 0){
		printf("client ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
		bool vaild = true;

		char header_buf[BUF_SIZE];  //回复头的缓冲区
		char* file_buf;             //文件的缓冲区  动态申请
		memset(header_buf,'\0',BUF_SIZE);
		//判断文件存不存在
		struct stat file_stat;
		if(stat(filename,&file_stat) < 0)   //第一个参数文件路径  假定文件在当前路径下
			vaild = false;
		else{
			if(S_ISDIR(file_stat.st_mode))//判断是不是目录
				vaild = false;
			else if(file_stat.st_mode & S_IROTH){  //判断有没有读取权限
				int fd = open(filename,O_RDONLY);
				assert(fd != -1);
				//文件内容的缓冲区  很显然和header_buf栈区的内存 不连续
				file_buf = new char[file_stat.st_size+1];
				memset(file_buf,'\0',file_stat.st_size+1);
				if(read(fd,file_buf,file_stat.st_size) < 0)
					vaild = false;
			}
			else
				vaild =false;
		}

		if(vaild){
			//有效才返回真正的回复
			int len = 0;  //回复头缓冲区已经用的字节数
			int ret = sprintf(header_buf,"%s %s\r\n","HTTP/1.1",HTTP_ACK[0]);
			len += ret;
			ret = sprintf(header_buf+len,"%s: %d\r\n","Content-Length",(int)file_stat.st_size);
			len += ret;
			ret = sprintf(header_buf+len,"%s","\r\n");  

			//利用wrtiev将header_buf和file_buf的内容一并写出
			struct iovec iv[2];
			iv[0].iov_base = header_buf;
			iv[0].iov_len = strlen(header_buf);
			iv[1].iov_base = file_buf;
			iv[1].iov_len = strlen(file_buf);
			ret = writev(confd,iv,2);
		}else{
			//无效返回错误
			int ret = sprintf(header_buf,"%s %s\r\n","HTTP/1.1",HTTP_ACK[1]);
			int len = 0;
			len += ret;
			ret = sprintf(header_buf+len,"%s","\r\n");
			send(confd,header_buf,strlen(header_buf),0);
		}
		close(confd);
		delete []file_buf;

	}
	else{
		printf("accept error\n");
	
	}

	close(sockfd);
	return 0;
}
