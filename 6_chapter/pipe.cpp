#include<stdio.h>
#include<unistd.h>
#include<assert.h>
#include<string.h>
#include<sys/types.h>
#include<sys/wait.h>
#define BUFSIZE 1024

int main()
{

	/*fd[0] 读   fd[1]  写*/
	int fd[2];
	int ret = pipe(fd);
	assert(ret != -1);
	
	pid_t pid = fork();
	if(pid > 0){  //父进程负责写
		close(fd[0]);
		char buf[BUFSIZE] = "Tomorrow is a great day\n";
		write(fd[1],buf,sizeof(buf));
		
		/*int status;
		wait(&status);*/

		while(1)
			sleep(1);
	}
	else if(pid == 0){//子进程负责读
		close(fd[1]);
		char buf[BUFSIZE];
		memset(buf,'\0',BUFSIZE);
		int ret = read(fd[0],buf,sizeof(buf));
		printf("son recv:%s",buf);
	}


	return 0;
}
