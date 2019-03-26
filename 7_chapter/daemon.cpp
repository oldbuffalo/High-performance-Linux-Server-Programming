#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>

//将服务器程序以守护进程的方式运行

bool daemonize(){
	//创建子进程  关闭父进程
	pid_t pid = fork();
	if(pid < 0)
		return false;
	else if(pid > 0)
		exit(0);

	//设置文件权限掩码  创建文件时 文件掩码是 mode & 0777  
	umask(0);  //这里不是很懂

	//创建新的会话 设置本进程位进程组的首领

	/*
	关于会话  session
	setsid 不能由进程组的首领进程调用
	对于非组首领的进程，调用该函数能创建新会话
	别的作用:
	1.调用进程成为会话的首领，此时该进程是新会话的唯一成员
	2.新建一个进程组  其pgid就是调用进程的pid，调用进程成为该组的首领
	3.调用进程将甩开终端
	*/
	pid_t sid = setsid();
	if(sid < 0)
		return false;

	//切换工作目录  切换到根目录
	if(chdir("/") < 0)
		return false;

	//关闭标准输入设备、标准输出设备和标准错误输出设备
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	
	//关闭其他已经打开的文件描述符  如果有的话

	//将标准输入、标准输出、标准错误重定向到/dev/null文件
	open("/dev/null",O_RDONLY);
	open("/dev/null",O_RDWR);
	open("/dev/null",O_RDWR);
	
	return true;
}

int main()
{

	daemonize();

	return 0;
}
