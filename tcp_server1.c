#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <unistd.h>
#include <poll.h>

#define MAX_CONNECTIONS 4 /*最大连接数*/
#define BAGLOG 100
#define BUFSIZE 8192
#define BUFLEN 1024

int useage()
{
	printf("\nUseage: tcp_server [option]\n");
	printf("\t%-25s-- set the tcp port\n", "-p");
	printf("\t%-25s-- set the listen ip\n", "-H");
	printf("\t%-25s-- help message\n", "-h");
	return 0;
}

static int tcp_listen(char *ip, int port)
{
	int listen_sockfd;
	struct sockaddr_in server_addr;
	int reuse_addr = 1;

	/* 
		PF_INET, AF_INET: IPV4 
		AF_INET（地址族）PF_INET（协议族）
	*/
	if (MAX_CONNECTIONS < 3) {
		printf("MAX_CONNECTIONS is too small:%d\n", MAX_CONNECTIONS);
		return -1;
	}

	listen_sockfd = socket(PF_INET, SOCK_STREAM, 0);      /* SOCK_STREAM mean TCP, SOCK_DGRAM mean UDP */
	bzero(&server_addr, sizeof(server_addr));                /* just like memset() */
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	if (!ip) {
		server_addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		server_addr.sin_addr.s_addr = inet_addr(ip);
	}

	setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

	if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
		printf("bind error\n");
		return -1;
	}

	printf("bind success\n");

	if (listen(listen_sockfd, BAGLOG) == -1) {
		printf("listen error\n");
		return -1;
	}

	printf("listen success\n");
	return listen_sockfd;
}

static void do_poll(int listen_sockfd)
{
	int i, maxfd = 1;
	struct pollfd clientfds[MAX_CONNECTIONS];
	char recmsg[BUFLEN];
	char sendmsg[BUFLEN];

	clientfds[0].fd = 0;
	clientfds[0].events = POLLIN;
	clientfds[1].fd = listen_sockfd;
	clientfds[1].events = POLLIN;

	for (i = 2; i < MAX_CONNECTIONS; i ++) {
		clientfds[i].fd = -1;
	}

	while (1) {
		int nready = poll(clientfds, maxfd + 1, 5000);

		if (nready == -1) {
			printf("poll error:\n");
			break;
		} else if (nready == 0) {
			continue;
		} else {
			if (clientfds[0].revents & POLLIN) {
				/* console input */
				bzero(recmsg, BUFLEN);
				fgets(recmsg, BUFLEN, stdin);

				if(!strncasecmp(recmsg, "exit", 4)){  
					printf("server request to exit!\n");  
					goto end;  
				}

				if(!strncmp(recmsg, "\n", 1)){  
					//printf("ignore this\n");  
				} else {
					int len;
					/* send to all clients */
					bzero(sendmsg, BUFLEN);
					sprintf(sendmsg, "server say:\n%s", recmsg);
					for (i = 2; i < MAX_CONNECTIONS; i ++) {
						if(clientfds[i].fd > 0) len = send(clientfds[i].fd, sendmsg, strlen(sendmsg), 0);
						//if(len <= 0) printf("send failed\n");
					}
				}

			} else if (clientfds[1].revents & POLLIN) {
				int client_fd = -1;
				socklen_t len;
				struct sockaddr_in c_addr;

				printf("here\n");
				/* recieve a connection */
				len = sizeof(struct sockaddr);

				if ((client_fd = accept(listen_sockfd, (struct sockaddr *)&c_addr, &len)) == -1) {
					printf("accept error\n");
				} else {

					printf("received a connection from %s:%d, client_fd %d\n", inet_ntoa(c_addr.sin_addr), ntohs(c_addr.sin_port), client_fd);
					for (i = 2; i < MAX_CONNECTIONS; i ++) {
						if (clientfds[i].fd < 0) {
							clientfds[i].fd = client_fd;
							clientfds[i].events = POLLIN;
							break;
						}
					}

					if (i == MAX_CONNECTIONS) {
						printf("too many clients.\n");
						bzero(sendmsg, BUFLEN);
						sprintf(sendmsg, "server have too many clients, will be close...\n");
						send(client_fd, sendmsg, strlen(sendmsg), MSG_DONTWAIT);
						close(client_fd);
						client_fd = -1;
					}

					maxfd = (i > maxfd ? i : maxfd);
				}

			} else {
				int recv_len, fd_count;
				struct sockaddr_in remote_addr;
				socklen_t r_len = sizeof(struct sockaddr);

				/* send to other clients */
				for (i = 2; i < MAX_CONNECTIONS; i ++) {
					if (clientfds[i].fd <= 0) continue;

					if (clientfds[i].revents & POLLIN) {

						bzero(recmsg, BUFLEN);
						recv_len = recv(clientfds[i].fd, recmsg, BUFLEN, 0);
						getpeername(clientfds[i].fd, (struct sockaddr *)&remote_addr, &r_len);

						if (recv_len > 0) {
							printf("received from %s:%d: %s", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port), recmsg);
							bzero(sendmsg, BUFLEN);
							sprintf(sendmsg, "%s:%d say:\n%s", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port), recmsg);

							for (fd_count = 2; fd_count < MAX_CONNECTIONS; fd_count ++) {
								if (fd_count == i || clientfds[fd_count].fd < 0) continue;

								if (send(clientfds[fd_count].fd, sendmsg, strlen(sendmsg), 0) < 0) {
									printf("send error: %d i: %d listen_sockfd: %d\n", fd_count, i, listen_sockfd);
									//continue;
								}
							}
						} else if (recv_len < 0) {
							printf("received failed\n");
							//continue;
						} else {
							printf("client socket error, %s:%d disconnect...\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));
							close(clientfds[i].fd);
							clientfds[i].fd = -1;
							if (maxfd == i) maxfd --;
							//continue;
						}
					}
				}
			}
		}
	}

end:
	for (i = 1; i < MAX_CONNECTIONS; i ++) {
		if (clientfds[i].fd > 0) close(clientfds[i].fd);
	}
	printf("poll function end!\n");
}

int main(int argc, char **argv)
{
	int listen_sockfd;
	int port = 8080;
	char *listen_addr = NULL;
	int x;

	if (argc % 2 == 0) {
		useage();
		return 0;
	}

	for (x = 1; x < argc; x += 2) {
		if (!strncmp(argv[x], "-p", 2)) {
			port = atoi(argv[x + 1]);
		} else if (!strncmp(argv[x], "-H", 2)) {
			listen_addr = argv[x + 1];
		} else {
			useage();
			return 0;
		}
	}

	/* check port */
	if (port <= 0) {
		port = 8080;
		printf("port init error, use default 8080\n");
	}

	listen_sockfd = tcp_listen(listen_addr, port);

	if (listen_sockfd < 0) goto end;

	do_poll(listen_sockfd);

end:
	printf("tcp server end\n");
	return 0;
}