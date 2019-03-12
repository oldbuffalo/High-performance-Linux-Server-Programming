#include<iostream>
using namespace std;
/*
socket地址  分为通用socket和专用socket

#include<bits/socket.h>
通过socket地址
struct sockaddr{
	sa_family_t sa_family;  地址族   Unix/Ipv4/Ipv6
	char sa_data[14];     存放socket地址值   不同的协议族地址不一样
};
协议族      地址值
PF_UNIX     文件路径名，108字节
PF_INET     16位端口号，32位IP，6字节
PF_INET6    16位端口号，32位流标识，128位IPv6地址，32位范围ID,26字节

新的通用地址
struct sockaddr_storage{
	sa_family_t sa_family;
	unsigned long int __sa_align;   //用来内存对其  ？？？
	char __sa_padding[128-sizeof(__ss_align)];  //???
};


专用socket地址
sockaddr_un
sockaddr_in
sockaddr_in6

所有的专用socket地址(包括sockaddr_storage)类型的变量在实际使用时都需要转化为通用socket地址类型
sockaddr(强转就行),因为所有的socket编程接口使用的地址参数都是sockaddr

IP地址转换函数
#include<arpa/inet.h>
inet_addr    将用点分十进制表示的字符串->网络字节序整数标识的IPv4地址,失败返回INADDR_NONE,成功返回结果
inet_aton    同上，不同的是将结果保存在传入的参数中  成功返回1，失败返回0
inet_ntoa    网络字节序整数表示的IPv4地址->点分十进制字符串,成功返回字符串
对于inet_ntoa，需要注意的是其内部用一个静态变量来存储转换过程，函数返回值指向该静态内存。是个不可重入函数

inet_pton   点分十进制->网络字节序   成功返回1，失败返回0并设置errno
inet_ntop   网络字节序->点分十进制  注意第四个参数，指定目标存储单元的大小  ipv4和ipv6不同，有两个宏
这两个转换函数同时使用于IPv4和IPv6 上面3个只适合IPv4

*/
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
					 

int main()
{
	in_addr_t l1,l2;
	l1 = inet_addr("1.2.3.4");
	l2 = inet_addr("127.0.0.1");
	struct in_addr addr1,addr2;
	memcpy(&addr1,&l1,4);
	memcpy(&addr2,&l2,4);

	char* pValue1 = inet_ntoa(addr1);
	char* pValue2 = inet_ntoa(addr2);
	cout<<pValue1<<endl;
	cout<<pValue2<<endl;
	cout<<inet_ntoa(addr1)<<endl;
	cout<<inet_ntoa(addr2)<<endl;

	return 0;
}
