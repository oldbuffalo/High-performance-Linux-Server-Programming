#include<stdio.h>
#include<unistd.h>



//uid  真实用户id  也就是启动程序的用户id
//euid 有效用户的id   主要是为了方便资源访问，使得运行程序的用户拥有该程序的有效用户的权限
int main()
{
	uid_t uid = getuid();
	uid_t euid = geteuid();

	printf("userid is %d,effective userid is %d\n",uid,euid);


	return 0;
}
