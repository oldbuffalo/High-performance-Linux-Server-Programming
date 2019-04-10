#include<stdio.h>
#include<sys/types.h>
#include<sys/sem.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>


union semun{
	int val;
	struct semid_ds* buf;
	unsigned short* array;
	struct seminfo* _buf;
};


//op为-1执行P操作 op为1执行V操作
void pv(int sem_id,int op){
	struct sembuf sem_b;
	sem_b.sem_num = 0;
	sem_b.sem_op = op;
	sem_b.sem_flg = SEM_UNDO;
	semop(sem_id,&sem_b,1);
}


int main()
{
	//IPC_PRIVATE 无论信号量是否存在  semget都将创建一个新的信号量  别的访问也能访问
	int sem_id = semget(IPC_PRIVATE,1,0666);

	union semun sem_un;
	sem_un.val = 1;
	semctl(sem_id,0,SETVAL,sem_un);

	pid_t pid = fork();
	if(pid < 0)
		return 1;
	else if(pid == 0){
		printf("child try to get binary sem\n");
		//在父子进程间共享IPC_PRIVATE 信号量的关键在于二者都可以操作该信号量的标识符sem_id
		pv(sem_id,-1);
		printf("child get the sem and would release it after 5 seconds\n");
		sleep(5);
		pv(sem_id,1);
		exit(0);
	}
	else{
		printf("parent try to get binary sem\n");
		pv(sem_id,-1);
		printf("parent get the sem and would release it after 5 seconds\n");
		sleep(5);
		pv(sem_id,1);
	}
	
	waitpid(pid,NULL,0); //阻塞等待子进程退出
	semctl(sem_id,0,IPC_RMID,sem_un);  //删除信号量

	return 0;
}
