#include<iostream>
using namespace std;

/*
字节序问题：字节在内存中的排列顺序
大端存储：低地址存高字节
小端存储：低地址存低字节
例如一个int类型的整数 0x12345678

地址空间  0x00 0x01 0x02 0x03
大端      0x12 0x34 0x56 0x78
小端      0x78 0x56 0x34 0x12

现在很多PC机都是小端，因此小端字节序也叫主机字节序，大端也叫网络字节序

为什么要有大小端？
如果同样一个32位的整数在两个不同字节序的主机之间直接传递，接受端必然会错误解释。
因此，需要将主机字节序统一转成网络字节序，然后根据自身的字节序，看是否将网络字节序转成主机字节序

主机的字节序是由厂商决定的。

*/

/*写代码如何判断大小端*/
/*
大小端转换函数
#include<netinet/in.h>
htonl    主机字节序->网络字节序   32位的  通常用来转换IP地址
htons                             16位的  通常用来转换端口号                         
ntohl    网络字节序->主机字节序  
ntohs
任何格式化的数据通过网络传输，都应该用这些函数来转换字节序
*/
union test{
	short a;
	char b[sizeof(short)];
};

void byteorder(){
	test t;
	t.a = 0x0102;   
	if(t.b[0] == 0x02 &&t.b[1] == 0x01)
		cout<<"little endian"<<endl;
	else if(t.b[0] == 0x01 && t.b[1] == 0x02)
		cout<<"big endian"<<endl;
	else
		cout<<"unknown"<<endl;
}

int main()
{
	//byteorder();
	int a = 0x12345678;
	char *p = (char*)&a;  //p是a中的第一个字节的地址(低地址)
	if(*p == 0x78)
		cout<<"little endian"<<endl;
	else if(*p == 0x12)
		cout<<"big endian"<<endl;
	else
		cout<<"unknow"<<endl;
	
	return 0;
}
