#include<netdb.h>
#include<assert.h>
#include<stdio.h>
#include<sys/types.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>

/*通过主机名和服务名访问目标服务器的daytime服务*/
int main(int argc,char* argv[])
{
	
	struct hostent* pHostinfo = gethostbyname(argv[1]);
	assert(pHostinfo);



	struct servent*  pServerinfo= getservbyname("daytime","tcp");
	assert(pServerinfo);
	printf("daytime port is %d\n",ntohs(pServerinfo->s_port));


	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = pServerinfo->s_port;
	addr.sin_addr = *(struct in_addr*)*pHostinfo->h_addr_list;

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(socket>0);

	int ret = connect(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	char buf[128];
	memset(buf,'\0',128);
	ret = read(sockfd,buf,sizeof(buf));
	assert(ret > 0);
	printf("the day time is %s\n",buf);

	close(sockfd);

	return 0;
}
