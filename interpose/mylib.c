#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

// copy from client.c
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>

#define MAXMSGLEN 100

int sockfd;

void initialize_client() {
	char *serverip;
	char *serverport;
	unsigned short port;
	// char *msg="Hello from client";
	// char buf[MAXMSGLEN+1];
	int rv;
	struct sockaddr_in srv;
	
	// Get environment variable indicating the ip address of the server
	serverip = getenv("server15440");
	if (serverip) fprintf(stderr, "Got environment variable server15440: %s\n", serverip);
	else {
		fprintf(stderr, "Environment variable server15440 not found.  Using 127.0.0.1\n");
		serverip = "127.0.0.1";
	}
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) fprintf(stderr, "Got environment variable serverport15440: %s\n", serverport);
	else {
		fprintf(stderr, "Environment variable serverport15440 not found.  Using 15440\n");
		serverport = "15440";
	}
	port = (unsigned short)atoi(serverport);
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to point to server
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
	srv.sin_port = htons(port);			// server port

	// actually connect to the server
	rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// // send message to server
	// printf("client sending to server: %s\n", msg);
	// send(sockfd, msg, strlen(msg), 0);	// send message; should check return value
	
	// // get message back
	// rv = recv(sockfd, buf, MAXMSGLEN, 0);	// get message
	// if (rv<0) err(1,0);			// in case something went wrong
	// buf[rv]=0;				// null terminate string to print
	// printf("client got messge: %s\n", buf);
	
}

// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
ssize_t (*orig_read)(int fildes, void *buf, size_t nbyte);
int (*orig_close)(int fildes);
ssize_t (*orig_write)(int fd, const void *buf, size_t count);
int (*orig_stat)(const char *restrict pathname, struct stat *restrict statbuf);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_unlink)(const char *pathname);
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes, off_t *restrict basep);
struct dirtreenode* (*orig_getdirtree)( const char *path );
void (*orig_freedirtree)( struct dirtreenode* dt );

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}

	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "mylib: open called for path %s\n", pathname);

	char* func_name = "open\n";
	send(sockfd, func_name, strlen(func_name), 0);

	return orig_open(pathname, flags, m);
}

ssize_t read(int fildes, void *buf, size_t nbyte) {
	char* func_name = "read\n";
	send(sockfd, func_name, strlen(func_name), 0);
	return orig_read(fildes, buf, nbyte);
}

ssize_t write(int fd, const void *buf, size_t count) {
	char* msg = "write\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_write(fd, buf, count);
}

int close(int fildes) {
	char* msg = "close\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_close(fildes);
}

int stat(const char *restrict pathname, struct stat *restrict statbuf) {
	char* msg = "stat\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_stat(pathname, statbuf);
}

off_t lseek(int fd, off_t offset, int whence) {
	char* msg = "lseek\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_lseek(fd, offset, whence);
}

int unlink(const char *pathname) {
	char* msg = "unlink\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_unlink(pathname);
}

ssize_t getdirentries(int fd, char *buf, size_t nbytes, off_t *restrict basep) {
	char* msg = "getdirentries\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_getdirentries(fd, buf, nbytes, basep);
}

struct dirtreenode* getdirtree( const char *path ) {
	char* msg = "getdirtree\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_getdirtree(path);
}

void freedirtree( struct dirtreenode* dt ) {
	char* msg = "freedirtree\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_freedirtree(dt);
}

// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_read = dlsym(RTLD_NEXT, "read");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_write = dlsym(RTLD_NEXT, "write");
	orig_stat = dlsym(RTLD_NEXT, "stat");
	orig_lseek = dlsym(RTLD_NEXT, "lseek");
	orig_unlink = dlsym(RTLD_NEXT, "unlink");
	orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
	orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
	orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");
	fprintf(stderr, "Init mylib\n");

	initialize_client();
}


