/* httpd.c */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>


#define LISTENADDR "127.0.0.1"

/* structures */
struct sHttpRequest {

	char method[8];
	char url[128];

};
typedef struct sHttpRequest httpreq;

struct sFile {
	char filename[64];
	char *fcontent;
	int size;
};
typedef struct sFile File;


/* global */
char *errorMsg;

/* returns 0 on error, or returns socket fd */
int server_init(int _port)
{
	int sfd; // server file descriptor
	struct sockaddr_in server;
	sfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sfd < 0)
	{
		errorMsg = "socket() error";
		return 0;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(LISTENADDR);
	server.sin_port = htons(_port);

	// on success zero is returned, on error -1 is returned.
	if (bind(sfd, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		close(sfd);
		errorMsg = "bind() error";
		return 0;
	}

	if (listen(sfd, 5))
	{
		close(sfd);
		errorMsg = "listen() error";
		return 0;
	}

	return sfd;
}

/*returns 0 on error, or returns the new client's socket file descriptor*/
int client_accept(int sfd)
{
	int cfd; // client file descriptor
	socklen_t addrlen;
	struct sockaddr_in client;


	addrlen = 0;
	memset(&client, 0, sizeof(client));
	cfd = accept(sfd, (struct sockaddr*)&client, &addrlen);

	if (cfd < 0)
	{
		errorMsg = "accept() error";
		return 0;
	}
	return cfd;

}

/* returns 0 on error otherwise returns httpreq structure */
httpreq *parse_http(char *str)
{

	httpreq *req;
	char *p;

	req = malloc(sizeof(httpreq));
	memset(req, 0, sizeof(httpreq));

	for(p=str; *p && *p != ' '; p++);
	if(*p == ' ') {
		*p = 0;
		goto copy;
	}
	else
	{
		errorMsg = "parse_http() NO SPACE";
		free(req);
		return 0;

	}
	copy:
		strncpy(req->method, str, 7);

	for(str=++p; *p && *p != ' '; p++);
	if(*p == ' ')
		*p = 0;
	else
	{
		errorMsg = "parse_http() NO SPACE - 2ND LOOP";
		free(req);
		return 0;

	}


	strncpy(req->url, str, 127);

	return req;

}


/* returns 0 on error, otherwise returns the data */
char *client_read(int cfd)
{
	static char buf[512];
	memset(buf, 0, 512);
	if(read(cfd, buf, 511)<0)
	{
		errorMsg = "read() ERROR";
		return 0;
	}
	else
		return buf;

}

void http_response(int cfd, char *contenttype, char *data)
{
	char buf[512];
	int n;

	n = strlen(data);
	memset(buf, 0, 512);
	snprintf(buf, 511,
		"Content-Type: %s\n"
		"Content-Length: %d\n"
		"\n%s\n",
		contenttype, n, data);

	n = strlen(buf);
	write(cfd, buf, n);

	return;
}

void http_headers(int cfd, int status)
{
	char buf[512];
	int writtenBytes;

	memset(buf, 0, 512);
	snprintf(buf, 511, "Http/1.0 %d OK\n"
		"Server: httpd.c\n"
		"Cache-Control: no-store, no-cache, max-age=0, private\n"
		"Content-Language: en\n"
		"Expires: -1\n"
		"X-Frame-Options: SAMEORIGIN\n",
		status
	);

	writtenBytes = strlen(buf);
	write(cfd, buf, writtenBytes);

	return;

}

// 0 for error, file structure for success
File *readfile(char *filename)
{
	char buf[512];
	int writtenBytes, readBytes, fd;
	char *pContent;
	File *file;

	fd = open(filename, O_RDONLY);
	if ( fd < 0 )
		return 0;


	file = malloc(sizeof(struct sFile));
	if (!file)
	{
		close(fd);
		return 0;
	}

	strncpy(file->filename, filename, 63);
	file->fcontent = malloc(512);

	readBytes = 0;
	while (1)
	{
		memset(buf, 0, 512);
		writtenBytes = read(fd, buf, 512);
		if (!writtenBytes)
			break;
		else if (readBytes == -1)
		{
			close(fd);
			free(file->fcontent);
			free(file);
			return 0;
		}

		memcpy(file->fcontent+readBytes, buf, writtenBytes);
		readBytes += writtenBytes;
		file->fcontent = realloc(file->fcontent, (512+readBytes));
	}

	file->size = readBytes;
	close(fd);
	return file;

}


// 0 for error, 1 for otherwise
int sendfile(int cfd, char *contenttype, File *filename)
{
	char buf[512];
	int writtenBytes, readBytes;
	char *pContent;

	if (!filename)
		return 0;

	memset(buf, 0, 512);
	snprintf(buf, 511,
		"Content-Type: %s\n"
		"Content-Length: %d\n\n",
		contenttype, filename->size);

	writtenBytes = filename->size;
	pContent = filename->fcontent;

	while(1)
	{

	readBytes = write(cfd, pContent, (writtenBytes < 512) ? writtenBytes : 512);

	if ( readBytes < 1 )
		return 0;

	writtenBytes -= readBytes;

	if (writtenBytes < 1)
		break;
	else
		pContent += readBytes;


	}

	return 1;
}

void client_connection(int sfd, int cfd)
{

	httpreq *req;
	char *p;
	char *res;
	char str[96];
	File *pFile;

	p = client_read(cfd);
	if (!p)
	{
		fprintf(stderr, "%s\n", errorMsg);
		close(cfd);
		return;
	}

	req = parse_http(p);
	if (!req)
	{
		fprintf(stderr, "%s\n", errorMsg);
		close(cfd);
		return;
	}

	if(!strcmp(req->method, "GET") && !strncmp(req->url, "/image/", 5))
	{

		if (strstr(req->url, ".."))
		{
			http_headers(cfd, 300);
			res = "Access Denied";
			http_response(cfd, "text/plain", res);
		}
		memset(str, 0, 96);
		snprintf(str, 95, ".%s", req->url);
		pFile = readfile(str);
		if (!pFile)
		{
			res = "File Not found";
			http_headers(cfd, 404);
			http_response(cfd, "text/plain",res);
		}
		else
		{
			http_headers(cfd, 200);
			if (!sendfile(cfd, "image/png", pFile))
			{
				res = "HTTP Server Error";
				http_response(cfd, "text/plain",res);
			}
		}
	}

	if ((!strcmp(req->method, "GET")) && (!strcmp(req->url, "/app/webpage")))
	{

		res = "<html><h1>Hello World</h1></html>";
		http_headers(cfd, 200);
		http_response(cfd, "text/html",res);

	}
	else
	{
		res = "File Not found";
		http_headers(cfd, 404);
		http_response(cfd, "text/plain",res);
	}

	free(req);
	close(cfd);

	return;
}

int main(int argc, char *argv[])
{

	int sfd, cfd;
	char *port;

	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <listening port>\n", argv[0]);
		return -1;
	}
	else
		port = argv[1];

	sfd = server_init(atoi(port));

	if(!sfd)
	{
		fprintf(stderr, "%s\n", errorMsg);
		return -1;
	}


	printf("Listening on %s:%d\n", LISTENADDR, atoi(port));
	while(1)
	{
		cfd = client_accept(sfd);
		if (!cfd)
		{
			fprintf(stderr, "%s\n", errorMsg);
			continue;
		}

		printf("Incoming connection\n");
		if(!fork())
		{
			client_connection(sfd, cfd);
		}

		// fork() is same as the SYS_FORK in assembly x86

	}

	return -1;
}
