#include<stdio.h>
#include<assert.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<sys/socket.h>

/*
在fork调用之后  父进程中打开的文件描述符在子进程中仍然保持打开
所以文件描述符可以很方便地从父进程传递到子进程
需要注意的是  传递一个文件描述符 不是传递一个文件描述符的值
而是要在接受进程中创建一个新的文件描述符，并且该文件描述符和发送进程
中被传递的文件描述符指向内核中相同的文件表项
*/
static const int CONTROL_LEN = CMSG_LEN(sizeof(int));

void send_fd(int fd,int fd_to_pass){
	struct iovec iov[1];
	struct msghdr msg;
	char buf[0];

	iov[0].iov_base = buf;
	iov[0].iov_len = 1;
	msg.msg_name= NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	cmsghdr cm;
	cm.cmsg_len = CONTROL_LEN;
	cm.cmsg_level = SOL_SOCKET;
	cm.cmsg_type = SCM_RIGHTS;
	*(int*)CMSG_DATA(&cm) = fd_to_pass;
	msg.msg_control = &cm;
	msg.msg_controllen = CONTROL_LEN;

	//通用数据读写函数
	sendmsg(fd,&msg,0);

}


int recv_fd(int fd){
	struct iovec iov[1];
	struct msghdr msg;
	char buf[0];

	iov[0].iov_base = buf;
	iov[0].iov_len = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	cmsghdr cm;
	msg.msg_control = &cm;
	msg.msg_controllen = CONTROL_LEN;

	recvmsg(fd,&msg,0);

	int fd_to_pass = *(int*)CMSG_DATA(&cm);

	return fd_to_pass;

}

int main()
{
	int pipefd[2];
	int fd_to_pass;
	//创建父子进程减的管道
	int ret = socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
	assert(ret != -1);

	pid_t pid = fork();
	
	if(pid == 0){
		close(pipefd[0]);
		fd_to_pass = open("test.txt",O_RDWR,0666);
		//子进程通过管道将文件描述符发送到父进程
		//如果文件打开失败，则子进程将标准输入文件描述符发送到父进程
		send_fd(pipefd[1],(fd_to_pass > 0) ? fd_to_pass : 0);
		close(fd_to_pass);
		exit(0);
	}
	else if(pid > 0){
		close(pipefd[1]);
		//父进程从管道接受目标文件描述符
		fd_to_pass = recv_fd(pipefd[0]);  
		char buf[1024];
		memset(buf,'\0',1024);
		read(fd_to_pass,buf,sizeof(buf));
		printf("I got fd %d and data %s\n",fd_to_pass,buf);
		close(fd_to_pass);
	}

	return 0;
}
