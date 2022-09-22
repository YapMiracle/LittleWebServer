#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x)) // isspace()方法是检测该字符是否是空字符串，包括'\n', '\t'， ' '等

#define SERVER_STRING "Server: SongHao's http/0.1.0\r\n" //定义个人server名称

void accept_request(int);//处理从套接字上监听到的一个 HTTP 请求
void bad_request(int);//返回给客户端这是个错误请求，400响应码
void cat(int, FILE *);//读取服务器上某个文件写到 socket 套接字
void cannot_execute(int);//处理发生在执行 cgi 程序时出现的错误
void error_die(const char *);//把错误信息写到 perror 
void execute_cgi(int, const char *, const char *, const char *);//运行cgi脚本，这个非常重要，涉及动态解析
int get_line(int, char *, int);//读取一行HTTP报文
void headers(int, const char *);//返回HTTP响应头
void not_found(int);//返回找不到请求文件
void serve_file(int, const char *);//调用 cat 把服务器文件内容返回给浏览器。
int startup(u_short *);//开启http服务，包括绑定端口，监听，开启线程处理链接
void unimplemented(int);//返回给浏览器表明收到的 HTTP 请求所用的 method 不被支持。

//  处理监听到的 HTTP 请求，处理http报文buf：提取请求参数以及是否含有查询参数，没有理解的是cgi动态解析和stat的作用。
// 只处理了第一行：请求方式+url+HTTP版本号
void accept_request(void *from_client)
{
	int client = *(int *)from_client;
	//作为get_line的参数存储报文
	char buf[1024];
	//http报文的长度
	int numchars;
	// 
	char method[255];
	char url[255];
	char path[512];
	size_t i, j;
	struct stat st;
	int cgi = 0;
	char *query_string = NULL;

	numchars = get_line(client, buf, sizeof(buf));//拿到buf中的http报文

	i = 0;
	j = 0;
	while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
	{
		//提取其中的请求方式，看看是get还是post
		method[i] = buf[j];
		i++;
		j++;
	}
	method[i] = '\0';

	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) // strcasecmp函数的作用是字符串的比较，但是不区分大小写。
	{
		unimplemented(client);
		return NULL;
	}

	if (strcasecmp(method, "POST") == 0)
		cgi = 1;

	i = 0;
	while (ISspace(buf[j]) && (j < sizeof(buf)))
		j++;

	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
	{
		url[i] = buf[j];
		i++;
		j++;
	}
	url[i] = '\0';

	// GET请求url可能会带有?,有查询参数
	if (strcasecmp(method, "GET") == 0)//比较，相同则返回0
	{

		query_string = url;
		while ((*query_string != '?') && (*query_string != '\0'))
			query_string++;

		/* 如果有?表明是动态请求, 开启cgi */
		if (*query_string == '?')
		{
			cgi = 1;
			*query_string = '\0';
			query_string++;
		}
	}

	sprintf(path, "httpdocs%s", url);// Write formatted data to string

	if (path[strlen(path) - 1] == '/')
	{
		strcat(path, "test.html");
	}

	//注意st是struct stat类型
	// https://www.cnblogs.com/matthew-2013/p/4679425.html，详细解释
	if (stat(path, &st) == -1)// stat函数是得到文件的信息，存储在st里面，int stat(const char *file_name, struct stat *buf);
	{
		while ((numchars > 0) && strcmp("\n", buf))
			numchars = get_line(client, buf, sizeof(buf));
		not_found(client);
	}
	else
	{
		if ((st.st_mode & S_IFMT) == S_IFDIR) // S_IFDIR代表目录
		//如果请求参数为目录, 自动打开test.html
		{
			strcat(path, "/test.html");
		}

		//文件可执行
		if ((st.st_mode & S_IXUSR) ||
			(st.st_mode & S_IXGRP) ||
			(st.st_mode & S_IXOTH))
			// S_IXUSR:文件所有者具可执行权限
			// S_IXGRP:用户组具可执行权限
			// S_IXOTH:其他用户具可读取权限
			cgi = 1;

		if (!cgi)
			serve_file(client, path);
		else
			execute_cgi(client, path, method, query_string);
	}

	close(client);
	// printf("connection close....client: %d \n",client);
	return NULL;
}

// 请求失败了，返回400状态，并且发送相关的信息
void bad_request(int client)
{
	char buf[1024];
	//发送400
	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "<P>Your browser sent a bad request, ");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "such as a POST without a Content-Length.\r\n");
	send(client, buf, sizeof(buf), 0);
}

void cat(int client, FILE *resource)
{
	//发送文件的内容
	char buf[1024];
	fgets(buf, sizeof(buf), resource);
	while (!feof(resource))
	{

		send(client, buf, strlen(buf), 0);
		fgets(buf, sizeof(buf), resource);
	}
}

//无法执行返回500状态
void cannot_execute(int client)
{
	char buf[1024];
	//发送500
	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	send(client, buf, strlen(buf), 0);
}

// 发生错误，输出错误信息
void error_die(const char *sc)
{
	perror(sc);
	exit(1);
}

//执行cgi动态解析，不知道是干嘛的
void execute_cgi(int client, const char *path,
				 const char *method, const char *query_string)
{

	char buf[1024];
	int cgi_output[2];
	int cgi_input[2];

	pid_t pid;
	int status;

	int i;
	char c;

	int numchars = 1;
	int content_length = -1;
	//默认字符
	buf[0] = 'A';
	buf[1] = '\0';
	if (strcasecmp(method, "GET") == 0)

		while ((numchars > 0) && strcmp("\n", buf))
		{
			numchars = get_line(client, buf, sizeof(buf));
		}
	else
	{

		numchars = get_line(client, buf, sizeof(buf));
		while ((numchars > 0) && strcmp("\n", buf))
		{
			buf[15] = '\0';
			if (strcasecmp(buf, "Content-Length:") == 0)
				content_length = atoi(&(buf[16]));

			numchars = get_line(client, buf, sizeof(buf));
		}

		if (content_length == -1)
		{
			bad_request(client);
			return;
		}
	}

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	if (pipe(cgi_output) < 0)
	{
		cannot_execute(client);
		return;
	}
	if (pipe(cgi_input) < 0)
	{
		cannot_execute(client);
		return;
	}

	if ((pid = fork()) < 0)
	{
		cannot_execute(client);
		return;
	}
	if (pid == 0) /* 子进程: 运行CGI 脚本 */
	{
		char meth_env[255];
		char query_env[255];
		char length_env[255];

		dup2(cgi_output[1], 1);
		dup2(cgi_input[0], 0);

		close(cgi_output[0]); //关闭了cgi_output中的读通道
		close(cgi_input[1]);  //关闭了cgi_input中的写通道

		sprintf(meth_env, "REQUEST_METHOD=%s", method);
		putenv(meth_env);

		if (strcasecmp(method, "GET") == 0)
		{
			//存储QUERY_STRING
			sprintf(query_env, "QUERY_STRING=%s", query_string);
			putenv(query_env);
		}
		else
		{	/* POST */
			//存储CONTENT_LENGTH
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			putenv(length_env);
		}

		execl(path, path, NULL); //执行CGI脚本
		exit(0);
	}
	else
	{
		close(cgi_output[1]);
		close(cgi_input[0]);
		if (strcasecmp(method, "POST") == 0)

			for (i = 0; i < content_length; i++)
			{

				recv(client, &c, 1, 0);

				write(cgi_input[1], &c, 1);
			}

		//读取cgi脚本返回数据

		while (read(cgi_output[0], &c, 1) > 0)
		//发送给浏览器
		{
			send(client, &c, 1, 0);
		}

		//运行结束关闭
		close(cgi_output[0]);
		close(cgi_input[1]);

		waitpid(pid, &status, 0);
	}
}

// 解析一行http报文（URL），解析的意思：利用recv函数将接受的报文存入buf中，返回值是长度
// sock参数是用来接受客户端发送的报文，buf用来保存报文，size是报文的长度
int get_line(int sock, char *buf, int size)
{
	int i = 0;
	char c = '\0';
	int n;

	while ((i < size - 1) && (c != '\n'))
	{
		n = recv(sock, &c, 1, 0);// 接受一个字符
		if (n > 0)
		{
			if (c == '\r')
			{
				n = recv(sock, &c, 1, MSG_PEEK);//MSG_PEEK参数：https://blog.csdn.net/G1036583997/article/details/49202405
				if ((n > 0) && (c == '\n'))
					recv(sock, &c, 1, 0);
				else
					c = '\n';
			}
			buf[i] = c;//存入buf中
			i++;
		}
		else
			c = '\n';
	}
	buf[i] = '\0';//字符串的结尾
	return (i);
}

// http请求的报头
void headers(int client, const char *filename)
{

	char buf[1024];

	(void)filename; /* could use filename to determine file type */
					//发送HTTP头
	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
}

//返回404错误页面，组装信息
void not_found(int client)
{
	char buf[1024];
	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "your request because the resource specified\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "is unavailable or nonexistent.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
}

//如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端即可
void serve_file(int client, const char *filename)
{
	FILE *resource = NULL;
	int numchars = 1;
	char buf[1024];
	buf[0] = 'A';
	buf[1] = '\0';
	while ((numchars > 0) && strcmp("\n", buf))
	{
		numchars = get_line(client, buf, sizeof(buf));
	}

	//打开文件
	resource = fopen(filename, "r");
	if (resource == NULL)
		not_found(client);
	else
	{
		headers(client, filename);
		cat(client, resource);
	}
	fclose(resource); //关闭文件句柄
}

//启动服务端
int startup(u_short *port)
{
	int httpd = 0, option;
	struct sockaddr_in name;
	//设置http socket
	httpd = socket(PF_INET, SOCK_STREAM, 0);
	if (httpd == -1)
		error_die("socket"); //连接失败

	socklen_t optlen;
	optlen = sizeof(option);
	option = 1;
	setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

	memset(&name, 0, sizeof(name));
	name.sin_family = AF_INET;
	name.sin_port = htons(*port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
		error_die("bind"); //绑定失败
	if (*port == 0)		   /*动态分配一个端口 */
	{
		socklen_t namelen = sizeof(name);
		if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
			error_die("getsockname");
		*port = ntohs(name.sin_port);
	}

	if (listen(httpd, 5) < 0)
		error_die("listen");
	return (httpd);
}

// 发送错误信息的http的消息，状态码是501
void unimplemented(int client)
{
	char buf[1024];
	//发送501说明相应方法没有实现
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
}

/*****************************主函数，也就是函数入口*****************************************/

int main(void)
{
	int server_sock = -1;
	u_short port = 6379; //默认监听端口号 port 为6379
	int client_sock = -1;
	struct sockaddr_in client_name;
	socklen_t client_name_len = sizeof(client_name);
	pthread_t newthread;
	server_sock = startup(&port);

	printf("http server_sock is %d\n", server_sock);
	printf("http running on port %d\n", port);
	while (1)
	{

		client_sock = accept(server_sock,
							 (struct sockaddr *)&client_name,
							 &client_name_len);

		printf("New connection....  ip: %s , port: %d\n", inet_ntoa(client_name.sin_addr), ntohs(client_name.sin_port));
		if (client_sock == -1)
			error_die("accept");

		if (pthread_create(&newthread, NULL, accept_request, (void *)&client_sock) != 0)
			perror("pthread_create");
	}
	close(server_sock);

	return (0);
}
