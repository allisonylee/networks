#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#define BACKLOG 10 // queue size
#define MAXDATASIZE 10000 // max number of bytes we can get at once 

void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);
int send_all(int fd, const char *buf, size_t len);
void send_error(int fd, const char *status, struct ParsedRequest *req);

/* TODO: proxy()
 * Establish a socket connection to listen for incoming connections.
 * Accept each client request in a new process.
 * Parse header of request and get requested URL.
 * Get data from requested remote server.
 * Send data to the client
 * Return 0 on success, non-zero on failure
*/
int proxy(char *proxy_port) {

  // establish a socket connection
  int sockfd, new_fd;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  struct sigaction sa;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, proxy_port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
            p->ai_protocol)) == -1) {
        perror("server: socket");
        continue;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
            sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
        close(sockfd);
        perror("server: bind");
        continue;
    }

    break;
  }

  freeaddrinfo(servinfo); // all done with this structure

  if (p == NULL)  {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("server: waiting for connections...\n");

  while(1) {  // main accept() loop for accepting socket connections
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
        &sin_size);
    if (new_fd == -1) {
        perror("accept");
        continue;
    }

    inet_ntop(their_addr.ss_family,
        get_in_addr((struct sockaddr *)&their_addr),
        s, sizeof s);
    printf("server: got connection from %s\n", s);

    if (!fork()) { // this is the child process
      close(sockfd); // child doesn't need the listener

      // receive info from client
      int numbytes, total = 0;
      char buf[MAXDATASIZE];
      while (total < MAXDATASIZE - 1) {
        numbytes = recv(new_fd, buf + total, MAXDATASIZE-1-total, 0);
        if (numbytes == -1) {
          perror("recv");
          exit(1);
        } else if (numbytes == 0) break; // client closed connection
        total += numbytes;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break; // we got the full header
      }

      if (total == 0) {
        close(new_fd);
        exit(0);
      }

      if (!strstr(buf, "\r\n\r\n")) {
        send_error(new_fd, "400 Bad Request", NULL);
      }

      // parse header of request and get requested URL.
      struct ParsedRequest *req = ParsedRequest_create();

      if (ParsedRequest_parse(req, buf, total) < 0) {
        printf("parse failed\n");
        send_error(new_fd, "400 Bad Request", req);
      }

      if (strcmp(req->method, "GET") != 0) {
        send_error(new_fd, "501 Not Implemented", req);
      }

      char *host = req->host;
      char *port = req->port ? req->port : "80";   // default HTTP port
      char *path = req->path; 

      // change headers
      ParsedHeader_set(req, "Host", req->host);
      ParsedHeader_set(req, "Connection", "close");

      // now get data from remote server
      size_t headerLen = ParsedHeader_headersLen(req);
      size_t requestLen = strlen(req->method) + strlen(path) + 32 + headerLen;
      char * out = malloc(requestLen);

      int n = snprintf(out, requestLen, "%s %s HTTP/1.0\r\n", req->method, path);
      if (ParsedRequest_unparse_headers(req, out + n, requestLen - n) < 0) { 
        free(out);
        send_error(new_fd, "500 Internal Server Error", req); 
      }
      size_t outlen = n + headerLen;

      struct addrinfo hints, *res, *p;
      int remote_fd;

      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;  
      hints.ai_socktype = SOCK_STREAM;

      if ((rv = getaddrinfo(host, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        free(out);
        send_error(new_fd, "500 Internal Server Error", req);
      }

      for (p = res; p != NULL; p = p->ai_next) {
        if ((remote_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
          continue;
        if (connect(remote_fd, p->ai_addr, p->ai_addrlen) == -1) {
          close(remote_fd);
          continue;
        }
        break;
      }
      freeaddrinfo(res);
      if (p == NULL) { 
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        free(out);
        send_error(new_fd, "500 Internal Server Error", req);
      }

      //send all bytes in teh request
      send_all(remote_fd, out, outlen);

      char rbuf[MAXDATASIZE];
      ssize_t k;
      while ((k = recv(remote_fd, rbuf, sizeof rbuf, 0)) > 0) {
        if (send_all(new_fd, rbuf, k) == -1) {
          close(remote_fd);
          free(out);
          send_error(new_fd, "500 Internal Server Error", req);
        }
      }

      close(remote_fd);
      close(new_fd);
      free(out);
      ParsedRequest_destroy(req);
      exit(0);
    }
    close(new_fd);  // parent doesn't need this
  }

  return 0;
}


int main(int argc, char * argv[]) {
  char *proxy_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./proxy <port>\n");
    exit(EXIT_FAILURE);
  }

  proxy_port = argv[1];
  return proxy(proxy_port);
}


void sigchld_handler(int s)
{
    (void)s; // quiet unused variable warning

    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t k = send(fd, buf + sent, len - sent, 0);
    if (k == -1) return -1;
    sent += k;
  }
  return 0;
}

// send an HTTP error status line to the client, clean up, and end the child
void send_error(int fd, const char *status, struct ParsedRequest *req) {
  char msg[64];
  int len = snprintf(msg, sizeof msg, "HTTP/1.0 %s\r\n\r\n", status);
  send_all(fd, msg, len);
  if (req) ParsedRequest_destroy(req);
  close(fd);
  exit(1);
}