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

//tee函数用于在两个管道文件描述符之间复制数据  

//该程序实现的功能  标准输入的数据 同时输出到终端和文件中
int main(int argc,char** argv)
{
	//传入filename  因此参数是2个
	if(argc <= 1){
		printf("usage:%s filename \n",argv[0]);
		exit(-1);
	}
	char* filename = argv[1];
	int fd = open(filename,O_CREAT|O_WRONLY|O_TRUNC,0664);
	assert(fd != -1);

	int pipe_stdout[2];
	int ret = pipe(pipe_stdout);
	assert(ret != -1);

	int pipe_file[2];
	ret = pipe(pipe_file);
	assert(ret != -1);

	//将标准输入定向到stdout管道
	ret = splice(STDIN_FILENO,NULL,pipe_stdout[1],NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
	assert(ret != -1);	
	
	//将stdout管道的输出 拷贝一份给 file管道
	ret = tee(pipe_stdout[0],pipe_file[1],32768,SPLICE_F_NONBLOCK);
	assert(ret != -1);

	//将file管道输出定向到文件
	ret = splice(pipe_file[0],NULL,fd,NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
	assert(ret != -1);	

	//将stdout定向到标准输出
	ret = splice(pipe_stdout[0],NULL,STDOUT_FILENO,NULL,32768,SPLICE_F_MORE|SPLICE_F_MOVE);
	assert(ret != -1);	
	

	close(fd);
	close(pipe_file[0]);
	close(pipe_file[1]);
	close(pipe_stdout[0]);
	close(pipe_stdout[1]);
	return 0;

}
