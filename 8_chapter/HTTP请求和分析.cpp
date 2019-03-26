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
#define BACKLOG     1024 /*listen 参数*/
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


LINE_STATUS parse_line(char* buf,int &check_index,int read_index){
	//从check_index位置开始分析，一直到read_index

	char temp;
	for(;check_index < read_index; check_index++){
		
		//获得当前要分析的字节
		temp = buf[check_index];

		//如果当前字节是"\r"，即回车符，则说明可能读取到一个完整的行
		if(temp == '\r'){
			//如果'\r'正好是目前buf中的最后一个已经被读入的客户数据，说明没有读取到一个完整的行
			//返回LINE_CONTIUNE表示还需要继续读取客户数据才能进一步分析
			if(check_index+1 == read_index)
				return LINE_CONTIUNE;
			else if(buf[check_index+1] == '\n'){//如果下一个字符是"\n"，说明成功读取到一个完整的行
				buf[check_index++] = '\0';
				buf[check_index++] = '\0';
				return LINE_OK;
			}
			else //否则的话  说明客户发送的HTTP请求存在语法问题
				return LINE_ERROR;
		}
		else if(temp == '\n'){ //如果当前的字节是'\n'，即换行符，则也说明可能读取到一个完整的行
			if(check_index > 1 && buf[check_index-1] == '\r'){
				buf[check_index-1]='\0';
				buf[check_index++] = '\0';
				return LINE_OK;
			}
			else //否则的话  说明客户发送的HTTP请求存在语法问题
				return LINE_ERROR;
		}
	}

	//如果所有内容都分析完毕也没遇到'\r'或者'\n'则返回LINE_CONTIUNE 表示还需要继续读取客户数据才能进一步分析

	return LINE_CONTIUNE;
}

/*分析请求行,此时已经确保扔进去的字符串是通过parse_line解析出来的完整一行请求行*/
/*请求行格式  请求方法(GET/POST) URL HTTP版本(HTTP/1.1)*/
HTTP_CODE parse_requestline(char* buf,CHECK_STATE &checkstate){
	char* url = strpbrk(buf," \t");
	//如果请求行中没有空白字符或者"\t"字符  说明HTPP请求有问题
	//例如 GET www.baidu.com HTTP/1.1  url返回T后面空格
	if(!url)
		return BAD_REQUEST;
	*url++ = '\0'; //GET\0www.baidu.com HTTP/1.1  url此时指向第一个w

	char* method = buf;
	if(strcasecmp(method,"GET") == 0)//忽略大小写的字符串比较
		cout<<"The request method is GET"<<endl;
	else
		return BAD_REQUEST;
	url += strspn(url," \t"); //用来取出url前面多余的空格
	//strspn作用
	//举个例子
	/*strspn() 函数用来计算字符串 str 中连续有几个字符都属于字符串 accept，其原型为：
	size_t strspn(const char *str, const char * accept);
	如果 str = "2016 abcd";  accept = "0123456789"; strspn(str,accept)输出4
	*/
	char* version = strpbrk(url," \t");
	if(!version)
		return BAD_REQUEST;
	*version++ = '\0'; 
	version += strspn(version," \t");
	//仅支持HTPP/1.1
	if(strcasecmp(version,"HTTP/1.1") != 0)
		return BAD_REQUEST;
	
	//检查URL是否合法
	if(strncasecmp(url,"http://",7) == 0){
		url += 7;
		/*原型为extern char *strchr(const char *s,char c)，可以查找字符串s中首次出现字符c的位置*/
		url = strchr(url,'/');
	}

	if(!url || url[0] != '/')
		return BAD_REQUEST;
	
	cout<<"The request URL is"<<url<<endl;

	//HTTP请求行处理完毕，状态转移到头部字段的分析
	checkstate = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

/*分析请求头部字段*/
HTTP_CODE parse_headers(char* buf){
	
	//遇到一个空行 说明我们得到了一个正确的HTPP请求
	if(buf[0] == '\0')
		return GET_REQUEST;
	else if(strncasecmp(buf,"Host:",5) == 0){ //处理HOST头部字段
		buf += 5;
		buf += strspn(buf," \t");
		cout<<"The request host is:"<<buf<<endl;
	}
	else //其他头部字段都不处理
		cout<<"I can not handle this header"<<endl;

	return NO_REQUEST;
}

/*HTTP请求入口函数，解析服务器每次读到的数据*/
HTTP_CODE parse_content(char* buf,int &checked_index,int read_index,
                        CHECK_STATE &checkstate,int &nextline_index){
	
	LINE_STATUS linestatus = LINE_OK;  //记录当前行的读取情况
	HTTP_CODE  retcode = NO_REQUEST;   //记录HTTP请求的处理结果
	
	//主状态机 用于从buf中取出所有完整的行
	while((linestatus = parse_line(buf,checked_index,read_index)) == LINE_OK){
		//只有是完整的行的时候才进入循环
		char* temp = buf+nextline_index; //完整行的开头
		nextline_index = checked_index;
		switch(checkstate){
			case CHECK_STATE_REQUESTLINE:  //分析请求行
			{
				retcode = parse_requestline(temp,checkstate);
				if(retcode == BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:   //分析头部字段
			{
				retcode = parse_headers(temp);
				if(retcode == BAD_REQUEST)
					return BAD_REQUEST;
				else if(retcode == GET_REQUEST)
					return GET_REQUEST;

				break;
			}
			default:
				return INTERNAL_ERROR;  //返回服务器内部错误
		}

	}

	//如果没有读取到一个完整的行，则表示还需要继续读取客户数据才能进一步分析
	if(linestatus == LINE_CONTIUNE)
		return NO_REQUEST;
	else
		return BAD_REQUEST;

	
	return retcode;

}




int main(int argc,char* argv[])
{

	//需要传入 ip  port   因此参数个数是3个
	if(argc <= 2){
		cout<<"usage: "<<basename(argv[0])<<" ip_address port_number"<<endl;
		return 1;
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	assert(sockfd >= 0);

	int reuse = 1;
	setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip,&addr.sin_addr);


	int ret;
	ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
	assert(ret != -1);

	ret = listen(sockfd,BACKLOG);
	assert(ret != -1);

	struct sockaddr_in client_addr;
	socklen_t nlen = sizeof(client_addr);


	int confd = accept(sockfd,(struct sockaddr*)&client_addr,&nlen);
	assert(confd >= 0);
	
	char buf[BUFFER_SIZE];  //请求的缓冲区
	memset(buf,'\0',BUFFER_SIZE);
	int checked_index = 0;
	int read_index = 0;    //缓冲区中已经有多少字节
	int nextline_index = 0;   /*下一行起始位置*/
	CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE; /*起始状态*/

	while(1){
		int datalen = recv(confd,buf+read_index,BUFFER_SIZE,0);
		if(datalen == -1){
			cout<<"read failed"<<endl;
			break;
		}
		else if(datalen == 0){
			cout<<"remote client has closed the connection"<<endl;
			break;
		}
		read_index += datalen;
		//将读取到的内容进行分析
		HTTP_CODE result = parse_content(buf,checked_index,read_index,checkstate,nextline_index);

		//尚未得到一个完整的HTTP请求
		if(result == NO_REQUEST)
			continue;
		else if(result == GET_REQUEST){  //得到一个完整的、正确的HTTP请求
			send(confd,szret[0],strlen(szret[0]),0);
			break;
		}
		else{   //其他情况表示发生错误
			send(confd,szret[1],strlen(szret[1]),0);
			break;
		}
	}
	
	close(confd);
	close(sockfd);

	return 0;
}
