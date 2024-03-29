/*
 * proxy.c - ICS Web proxy
 *
 * Junqi Xie @junqi-xie
 */

#include "csapp.h"
#include <stdarg.h>

/*
 * Thread parameters
 */
struct conn_info
{
    int connfd;
    struct sockaddr_storage clientaddr; /* Enough space for any address */
};
sem_t mutex; /* Mutex for logging */

/*
 * Function prototypes
 */
void *thread(void *vargp);
void proxy(int connfd, struct sockaddr_in *sockaddr);
ssize_t forward_header(rio_t *rio, int fd, ssize_t *size);
void forward_body(rio_t *rio, int fd, ssize_t *size, ssize_t content_length);
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen;
    struct conn_info *conn;
    pthread_t tid;

    /* Check arguments */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    Signal(SIGPIPE, SIG_IGN); /* Ignore SIGPIPE signals */
    Sem_init(&mutex, 0, 1); /* Initialize mutex */
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        conn = (struct conn_info *)Malloc(sizeof(struct conn_info));
        clientlen = sizeof(struct sockaddr_storage);
        conn->connfd = Accept(listenfd, (SA *)&(conn->clientaddr), &clientlen);
        Pthread_create(&tid, NULL, thread, conn);
    }
    exit(0);
}

void *thread(void *vargp)
{
    struct conn_info *conn = (struct conn_info *)vargp;
    int connfd = conn->connfd;
    struct sockaddr_storage clientaddr = conn->clientaddr;

    Pthread_detach(pthread_self());
    Free(vargp);
    proxy(connfd, (struct sockaddr_in *)&(clientaddr));
    Close(connfd);
    return NULL;
}

/*
 * proxy - read and proxy web contents until client closes connection
 */
void proxy(int connfd, struct sockaddr_in *sockaddr)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    int clientfd;
    ssize_t content_length, size;
    rio_t connrio, clientrio;

    Rio_readinitb(&connrio, connfd);
    if (!Rio_readlineb_w(&connrio, buf, MAXLINE))
        return;

    /* Read Request Line */
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3 || strcasecmp(version, "HTTP/1.1"))
    {
        fprintf(stderr, "Illegal request line\n");
        return;
    }
    if (parse_uri(uri, hostname, pathname, port))
    {
        fprintf(stderr, "Illegal URL\n");
        return;
    }
    sprintf(buf, "%s /%s %s\r\n", method, pathname, version);
    
    clientfd = Open_clientfd(hostname, port);
    Rio_readinitb(&clientrio, clientfd);
    Rio_writen_w(clientfd, buf, strlen(buf));

    /* Forward from client to server */
    size = 0;
    content_length = forward_header(&connrio, clientfd, &size);
    if (strcasecmp(method, "GET"))
        forward_body(&connrio, clientfd, &size, content_length);

    /* Forward from server to client */
    size = 0;
    content_length = forward_header(&clientrio, connfd, &size);
    forward_body(&clientrio, connfd, &size, content_length);

    /* Output Log */
    format_log_entry(buf, sockaddr, uri, size);
    P(&mutex);
    printf("%s\n", buf);
    V(&mutex);

    Close(clientfd);
}

ssize_t forward_header(rio_t *rio, int fd, ssize_t *size)
{
    ssize_t content_length;
    char buf[MAXLINE];

    *size += Rio_readlineb_w(rio, buf, MAXLINE);
    Rio_writen_w(fd, buf, strlen(buf));
    while (strcmp(buf, "\r\n"))
    {
        *size += Rio_readlineb_w(rio, buf, MAXLINE);
        if (!strncmp(buf, "Content-Length: ", 16))
            sscanf(buf, "Content-Length: %zd", &content_length);
        Rio_writen_w(fd, buf, strlen(buf));
    }

    return content_length;
}

void forward_body(rio_t *rio, int fd, ssize_t *size, ssize_t content_length)
{
    char buf[MAXLINE];

    while (content_length > 0)
    {
        Rio_readnb_w(rio, buf, 1);
        ++*size;
        --content_length;
        Rio_writen_w(fd, buf, 1);
    }
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0)
    {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':')
    {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    }
    else
    {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
        pathname[0] = '\0';
    }
    else
    {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    char host[INET_ADDRSTRLEN];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
        unix_error("Convert sockaddr_in to string representation failed\n");

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}
