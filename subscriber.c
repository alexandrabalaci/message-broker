#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <math.h>
#include "helpers.h"

#define MAX_LEN 1600
#define BUFFLEN 1551

struct msg_udp{
    char data_type;
    char topic[50];
    char payload[MAX_LEN];
};

struct msg_tcp{
    int action_type;
    char payload[50];
    int sf;
}__attribute__((packed));

struct client_tcp {
    int udp_port;
    char ip[16];
    char topic[51];
    char data_type[11];
    char payload[BUFFLEN];
};


void usage(char *file)
{
    fprintf(stderr, "Usage: %s server_address server_port\n", file);
    exit(0);
}

int main(int argc, char *argv[]){
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int sockfd, n, ret;
    struct sockaddr_in serv_addr;
    char buffer[BUFFLEN];
    struct msg_tcp tcp_msg;
    int portno;

    fd_set read_fds;
    fd_set tmp_fds;
    int fdmax;

    FD_ZERO(&tmp_fds);
    FD_ZERO(&read_fds);

    if (argc < 3){
        usage(argv[0]);
    }

    portno = atoi(argv[3]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);

    ret = inet_aton(argv[2], &serv_addr.sin_addr);
    DIE(ret == 0, "inet_aton");

    ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(ret < 0, "connect");

    int yes = 1;
    int result_tcp = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(int));
    DIE(result_tcp < 0, "tcp no_delay fail");

    strcpy(tcp_msg.payload, argv[1]);
    n = send(sockfd, (char *) &tcp_msg, sizeof(struct msg_tcp), 0);
    DIE(n < 0, "send");

    FD_SET(sockfd, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    fdmax = sockfd + 1;

    while (1){
        tmp_fds = read_fds;
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        for (int i = 0; i <= fdmax; i++){
            if (FD_ISSET(i, &tmp_fds)){ 
                /* STDIN */
                if (i == 0){
                    memset(buffer, 0, BUFLEN);
                    fgets(buffer, BUFLEN - 1, stdin);

                    if (strncmp(buffer, "exit", 4) == 0){
                        goto end;
                    }
                
                  char degeaba[BUFLEN];
                  /* comanda de subscribe */
                  if(strncmp(buffer, "subscribe", 9) == 0){
                    tcp_msg.action_type = 0;
                    sscanf(buffer, "%s %s %d", degeaba, &tcp_msg.payload, &(tcp_msg.sf));
                    n = send(sockfd, (char *) &tcp_msg, sizeof(struct msg_tcp), 0);
                    DIE(n < 0, "send");

                    printf("Subscribed to topic.\n");
                  }

                  /* comanda de unsubscribe */
                  if(strncmp(buffer, "unsubscribe", 11) == 0){
                    tcp_msg.action_type = 1;
                    sscanf(buffer, "%s %s %d", degeaba, &tcp_msg.payload, &(tcp_msg.sf));
                    n = send(sockfd, (char *) &tcp_msg, sizeof(struct msg_tcp), 0);
                    DIE(n < 0, "send");

                    printf("Unsubscribed from topic.\n");
                  }
            
                }
                else {
                    /* se primeste mesaj de la server */
                    struct client_tcp tcp_received;
                    memset(&tcp_received, 0, sizeof(tcp_received));
                    n = recv(sockfd, (char *)&tcp_received, sizeof(struct client_tcp), 0);
                    DIE(n < 0, "recv");

                   /* s-a inchis conexiunea de la server*/
                     if (n == 0){
                         goto end;
                     } 
                     if(strcmp(tcp_received.payload, "exit") == 0) {
                        goto end;
                    }  
                    
                    printf("%s:%hu - %s - %s - %s\n",tcp_received.ip, tcp_received.udp_port,
                    tcp_received.topic, tcp_received.data_type, tcp_received.payload);                    
                    
                }
            }
        }
    }

end:
    close(sockfd);
    return 0;
}
