/**
 * @file server.c
 * @brief Implements an RPC-based file server handling remote file operations.
 *
 * This server receives file operation requests from clients via TCP, processes
 * them locally, and sends responses back. It supports operations such as
 * open, read, write, close, stat, unlink, and directory traversal.
 *
 * Features:
 * - **Request Handling**: Uses `get_request()` to read incoming RPC requests.
 * - **Response Transmission**: Sends results back using `send()`.
 * - **Directory Tree Serialization**: Implements `serialize_dirtree()` to
 * convert hierarchical directory structures into a serialized format.
 * - **Concurrent Processing**: Uses `fork()` to handle multiple clients.
 * - **Socket Management**: Listens for incoming connections and processes them
 * in a loop.
 *
 * The `main()` function initializes the server, binds to the specified port,
 * and listens for incoming connections. Each request is processed based on its
 * opcode in `execute_request()`, ensuring efficient remote file system
 * interactions.
 */
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/dirtree.h"
#include "message.h"

#define MAXMSGLEN 1048575

// server:
// getrequest
// sendresponse

int get_request(request *req, int sessfd) {
  size_t header_len = sizeof(req_header);

  size_t read_cnt = 0;
  while (read_cnt < header_len) {
    // convert to char* to do pointer arithmetic
    ssize_t bytes_received =
        recv(sessfd, (char *)req + read_cnt, header_len - read_cnt, 0);
    if (bytes_received <= 0) {
      return -1;
    }
    read_cnt += bytes_received;
  }

  read_cnt = 0;
  fprintf(stderr, "server: func: %d, payload length: %ld\n", req->header.opcode,
          req->header.payload_len);

  while (read_cnt < req->header.payload_len) {
    fprintf(stderr, "try to read payload\n");
    ssize_t bytes_received = recv(sessfd, (char *)req + header_len + read_cnt,
                                  req->header.payload_len - read_cnt, 0);
    if (bytes_received <= 0) {
      return -1;
    }
    read_cnt += bytes_received;
  }

  fprintf(stderr, "server: func: %d\n", req->header.opcode);
  return 0;
}

char *serialize_dirtree(struct dirtreenode *root, size_t *size) {
  if (root == NULL) {
    fprintf(stderr, "This shouldn't happen\n");
    exit(255);
  }
  char **buffers = malloc(sizeof(char *) * (root->num_subdirs));
  size_t *lens = malloc(sizeof(size_t) * (root->num_subdirs));
  size_t subtree_buffer_len = 0;
  for (int i = 0; i < root->num_subdirs; i++) {
    size_t nbyte;
    buffers[i] = serialize_dirtree(root->subdirs[i], &nbyte);
    lens[i] = nbyte;
    subtree_buffer_len += nbyte;
  }
  size_t entryname_len = strlen(root->name) + 1;
  size_t total_len = entryname_len + sizeof(int) + subtree_buffer_len;
  char *res = (char *)malloc(total_len);

  char *offset = res;
  memcpy(offset, root->name, entryname_len);
  offset += entryname_len;
  memcpy(offset, &root->num_subdirs, sizeof(int));
  offset += sizeof(int);

  for (int i = 0; i < root->num_subdirs; i++) {
    memcpy(offset, buffers[i], lens[i]);
    free(buffers[i]);
    offset += lens[i];
  }
  *size = total_len;
  free(buffers);
  free(lens);
  return res;
}

void recursive_free(struct dirtreenode *dt) {
  if (dt == NULL)
    return;
  for (int i = 0; i < dt->num_subdirs; i++) {
    recursive_free(dt->subdirs[i]);
  }
  free(dt->name);
  free(dt->subdirs);
  free(dt);
}

void execute_request(request *req, int sessfd) {
  switch (req->header.opcode) {
  case OPEN:
    int fd = open(req->req.open.pathname, req->req.open.flags, req->req.open.m);
    response open_res = {.header.errno_value = errno,
                         .header.payload_len = sizeof(union res_union),
                         .res.open.ret_val = fd};
    send(sessfd, (void *)&open_res, sizeof(response), 0);
    break;
  case READ:
    response *read_response = malloc(MAXMSGLEN);
    size_t nbyte = read(req->req.read.fildes, read_response->res.read.buf,
                        req->req.read.nbyte);

    read_response->header.errno_value = errno;
    read_response->header.payload_len = sizeof(union res_union) + nbyte;
    read_response->res.read.nbyte = nbyte;

    send(sessfd, (void *)read_response, sizeof(response) + nbyte, 0);
    free(read_response);
    break;
  case WRITE:
    ssize_t cnt =
        write(req->req.write.fd, req->req.write.buf, req->req.write.count);
    response write_res = {.header.errno_value = errno,
                          .header.payload_len = sizeof(union res_union),
                          .res.write.ret_val = cnt};
    send(sessfd, (void *)&write_res, sizeof(response), 0);
    break;
  case CLOSE:
    int ret = close(req->req.close.fd);
    response close_res = {.header.errno_value = errno,
                          .header.payload_len = sizeof(union res_union),
                          .res.close.ret_val = ret};
    send(sessfd, (void *)&close_res, sizeof(response), 0);
    break;
  case LSEEK:
    off_t off =
        lseek(req->req.lseek.fd, req->req.lseek.offset, req->req.lseek.whence);
    response lseek_response = {
        .header.errno_value = errno,
        .header.payload_len = sizeof(union res_union),

        .res.lseek.off = off,
    };
    send(sessfd, (void *)&lseek_response, sizeof(response), 0);
    break;
  case STAT:
    response stat_response;
    stat_response.res.stat.ret_val =
        stat(req->req.stat.pathname, &stat_response.res.stat.statbuf);
    stat_response.header.errno_value = errno;
    stat_response.header.payload_len = sizeof(union res_union);
    send(sessfd, (void *)&stat_response, sizeof(response), 0);
    break;
  case UNLINK:
    int ret_val = unlink(req->req.unlink.pathname);
    response unlink_response = {
        .header.errno_value = errno,
        .header.payload_len = sizeof(union res_union),

        .res.unlink.ret_val = ret_val,
    };
    send(sessfd, (void *)&unlink_response, sizeof(response), 0);
    break;
  case GETDIRENTRIES:
    response *r = malloc(sizeof(response) + req->req.direntries.nbytes);
    ssize_t bytes_read =
        getdirentries(req->req.direntries.fd, r->res.direntries.buf,
                      req->req.direntries.nbytes, &r->res.direntries.basep);

    r->header.errno_value = errno;
    r->header.payload_len = sizeof(union res_union) + bytes_read;
    r->res.direntries.ret_val = bytes_read;

    send(sessfd, (void *)r, sizeof(response) + bytes_read, 0);
    free(r);
    break;
  case GETDIRTREE:
    struct dirtreenode *root = getdirtree(req->req.dirtree.path);
    size_t tree_nbyte = 0;
    char *buf = serialize_dirtree(root, &tree_nbyte);
    response *dirtree_response = malloc(sizeof(response) + tree_nbyte);
    dirtree_response->header.errno_value = errno;
    dirtree_response->header.payload_len = sizeof(union res_union) + tree_nbyte;
    memcpy(dirtree_response->res.dirtree.buf, buf, tree_nbyte);
    free(buf);

    recursive_free(root);
    root = NULL;
    freedirtree(root);

    send(sessfd, (void *)dirtree_response, sizeof(response) + tree_nbyte, 0);
    free(dirtree_response);
    break;
  default:
    break;
  }
}

int main(int argc, char **argv) {
  char *serverport;
  unsigned short port;
  int sockfd, sessfd, rv;
  struct sockaddr_in srv, cli;
  socklen_t sa_size;

  // Get environment variable indicating the port of the server
  serverport = getenv("serverport15440");
  if (serverport)
    port = (unsigned short)atoi(serverport);
  else
    port = 15440;

  // Create socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0); // TCP/IP socket
  if (sockfd < 0)
    err(1, 0); // in case of error

  // setup address structure to indicate server port
  memset(&srv, 0, sizeof(srv));            // clear it first
  srv.sin_family = AF_INET;                // IP family
  srv.sin_addr.s_addr = htonl(INADDR_ANY); // don't care IP address
  srv.sin_port = htons(port);              // server port

  // bind to our port
  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  rv = bind(sockfd, (struct sockaddr *)&srv, sizeof(struct sockaddr));
  if (rv < 0)
    err(1, 0);

  // start listening for connections
  rv = listen(sockfd, 5);
  if (rv < 0)
    err(1, 0);

  // main server loop, handle clients one at a time, quit after 10 clients
  while (1) {

    // wait for next client, get session socket
    sa_size = sizeof(struct sockaddr_in);
    sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
    if (sessfd < 0)
      err(1, 0);
    if (fork() == 0) {
      // child
      close(sockfd);
      while (1) {
        request *req = malloc(MAXMSGLEN);
        if (get_request(req, sessfd) != 0) {
          fprintf(stderr, "[server.c] Connection close.\n");
          free(req);
          close(sessfd);
          break;
        }
        execute_request(req, sessfd);
        free(req);
        fprintf(stderr, "[server.c] Finish one request.\n");
      }
      close(sessfd);
      exit(0);
    }
  }
  close(sockfd);

  return 0;
}
