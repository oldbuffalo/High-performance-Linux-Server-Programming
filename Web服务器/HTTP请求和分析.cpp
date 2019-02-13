#include<iostream>
#include<unistd.h>
#include<stdlib.h>
#include<libgen.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<assert.h>

#define BUFFER_SIZE 4096 /*读缓冲区大小*/

using namespace std;

static const char* szret[] = {
"I get a correct result\n",
"Something wrong\n"
};


/*主状态机  1.当前正在分析请求行 2.当前正在分析请求头部*/
enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER};

/*从状态机  1.完整读取一行 2.行出错 3.行数据尚且不完整*/
enum LINE_STATUS {LINE_OK = 0,LINE_ERROR,LINE_CONTIUNE};

/*处理HTTP请求的结果
NO_REQUEST        请求不完整，需要继续读取用户数据
GET_REQUEST       获得了一个完整的用户请求
BAD_REQUEST       用户请求有语法错误
FORBIDDEN_REQUEST 用户对资源没有足够的访问权限
INTERNAL_ERROR    服务器内部错误
CLOSED_CONNECTION 客户端已经关闭连接
*/
enum HTTP_CODE {NO_REQUEST = 0,GET_REQUEST,
                BAD_REQUEST,FORBIDDEN_REQUEST,
				INTERNAL_ERROR,CLOSED_CONNECTION};

/*从状态机，用于解析读入字符串的状态
如果有完整一行，返回交给主状态机处理----因此已经处理完毕的字符数量和读入字符数量不一定相等
checked_index-----已经处理完毕的字符索引
read_index--------至今总共读入的字符索引
*/
LINE_STATUS parse_line(char* buf,int &checked_index,int read_index){

	return LINE_OK;
}

/*分析请求行,此时已经确保扔进去的字符串是通过parse_line解析出来的完整一行请求行*/
HTTP_CODE parse_requestline(char* buf){
	

	return NO_REQUEST;
}

/*分析请求头部*/
HTTP_CODE parse_requestheader(char* buf){


	return NO_REQUEST;
}

/*HTTP请求入口函数，解析服务器每次读到的数据*/
HTTP_CODE parse_content(char* buf,int &checked_index,int read_index,
                        CHECK_STATE &checkstate,int &nextline_index){
	



	return NO_REQUEST;
}




int main(int argc,char* argv[])
{
	if(argc < 2){
		cout<<"usage: "<<basename(argv[0])<<" ip_address port_number"<<endl;
		return 1;
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd >= 0);

	int ret;
	ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	ret = listen(sockfd,128);
	assert(ret != -1);

	struct sockaddr_in client_addr;
	socklen_t nlen = sizeof(client_addr);
	int fd = accept(sockfd,(struct sockaddr*)&client_addr,&nlen);
	assert(fd >= 0);
	
	char buf[BUFFER_SIZE];
	memset(buf,0,BUFFER_SIZE);
	int checked_index = 0;
	int read_index = 0;
	int datalen = 0;
	int nextline_index = 0;   /*下一行起始位置*/
	CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE; /*起始状态*/
	while(1){
		datalen = recv(sockfd,buf+read_index,BUFFER_SIZE,0);
		if(datalen == -1){
			cout<<"read failed"<<endl;
			break;
		}
		else if(datalen == 0){
			cout<<"remote client has closed the connection"<<endl;
			close(fd);
			break;
		}
		read_index += datalen;
		HTTP_CODE result = parse_content(buf,checked_index,read_index,checkstate,nextline_index);
		if(result == NO_REQUEST)
			continue;
		else if(result == GET_REQUEST){
			send(fd,szret[0],strlen(szret[0]),0);
			break;
		}
		else{
			send(fd,szret[1],strlen(szret[1]),0);
			break;
		}
	}
	
	close(fd);
	close(sockfd);

	return 0;
}
