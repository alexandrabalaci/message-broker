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

#define BUFFLEN 1501
#define MAX(a,b) (((a)>(b))?(a):(b))

struct msg_udp {
    int data_type;
    char topic[50];
    char payload[BUFFLEN];
};

/*strucutra unui mesaj tcp primit de la client*/
struct msg_tcp {
    int action_type;  //0 = subscribe // 1 = unsubscribe
    char payload[50];
    int sf;
};

/*structura unui mesaj tcp trimis *catre* un client*/
struct client_tcp {
    int udp_port;
    char ip[16];
    char topic[51];
    char data_type[11];
    char payload[BUFFLEN];

};

/*structura informatiilor despre clienti*/
struct client {
    int socket;
    char id[10];
    int online;
    int sf;
    int subscribed;
    int has_waiting_msg;
}__attribute__((packed));


/*structura unui topic*/
struct topic {
    char name[50];
    char type;
    //char subscribers[100][10];
    struct client subscribers[50];
    int subs_no;
}__attribute__((packed));

/* structura pentru retinerea mesajelor bufferate pentru fiecare client*/
struct buffered_msg{
    char client_id[10];
    struct client_tcp buffered_msg[30];
    int msg_no;
};

int client_already_registered(struct client *clients, int clients_no, char *id) {
    for (int i = 0; i < clients_no; i++) {
        if (strcmp(clients[i].id, id) == 0)
            return i;
    }
    return -1;
}

int topic_already_registered(struct topic *topics, int topics_no, char *topic) {
    for (int i = 0; i < topics_no; i++) {
        if (strcmp(topics[i].name, topic) == 0)
            return i;
    }
    return -1;
}

int client_subscribed_to_topic(char **subscribers, int subs_no, char *client_name){
    for(int i = 0; i < subs_no; i++){
        if(strcmp(subscribers[i], client_name) == 0)
            return i;
    }
    return -1;
}

void usage(char *file) {
    fprintf(stderr, "Usage: %s server_port\n", file);
    exit(0);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int sock_tcp, newsock_tcp, portno;
    char buffer[BUFFLEN];

    int sock_udp;
    struct msg_udp msg_udp;

    struct sockaddr_in serv_addr, cli_addr, udp_addr;
    int n, i, ret;
    socklen_t clilen, udplen;

    struct client *clients = malloc(sizeof(struct client) * 50);
    struct topic *topics = malloc(sizeof(struct topic) * 100);
    struct buffered_msg *buffered_msg = malloc(sizeof(struct buffered_msg) * 100);
    int buffered_msg_no;
    int clients_no = 0;
    int topics_no = 0;

    fd_set read_fds;
    fd_set tmp_fds;
    int fdmax;

    if (argc < 2) {
        usage(argv[0]);
    }

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    sock_udp = socket(AF_INET, SOCK_DGRAM, 0);

    int yes = 1;
    DIE(sock_tcp < 0, "socket_tcp");
    DIE(sock_udp < 0, "socket_udp");

    portno = atoi(argv[1]);
    DIE(portno == 0, "atoi");

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sock_tcp, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    memset((char *) &udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(portno);
    udp_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sock_udp, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = listen(sock_tcp, MAX_CLIENTS);
    DIE(ret < 0, "listen");

    /* se adauga noul file descriptor (socketul pe care se asculta conexiuni) in multimea read_fds*/
    FD_SET(sock_tcp, &read_fds);
    FD_SET(sock_udp, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    fdmax = MAX(sock_tcp, sock_udp) + 1;

    while (1) {
        tmp_fds = read_fds;

        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &tmp_fds)) {
                if (i == 0) {
                    /* STDIN */
                    fgets(buffer, BUFFLEN - 1, stdin);
                    /*close everything and go home*/
                    if (strncmp(buffer, "exit", 4) == 0) {
                        goto end;
                    }
                } else if (i == sock_tcp) {
                    /*a venit o cerere de conexiune pe socketul inactiv (cel cu listen),pe care serverul o accepta*/
                    clilen = sizeof(cli_addr);
                    newsock_tcp = accept(sock_tcp, (struct sockaddr *) &cli_addr, &clilen);
                    DIE(newsock_tcp < 0, "accept");

                    int result_tcp = setsockopt(newsock_tcp, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(int));
                    DIE(result_tcp < 0, "tcp no_delay fail");

                    /* se adauga noul socket intors de accept() la multimea descriptorilor de citire*/
                    FD_SET(newsock_tcp, &read_fds);
                    if (newsock_tcp > fdmax) {
                        fdmax = newsock_tcp;
                    }

                    struct msg_tcp tcp_aux;
                    n = recv(newsock_tcp, (char *) &tcp_aux, sizeof(struct msg_tcp), 0);
                    DIE(n < 0, "recv");
                    int client_index = client_already_registered(clients, clients_no, tcp_aux.payload);
                    if (client_index == -1) {
                        /*se primeste un client nou tcp si este adaugat in lista de clienti*/
                        clients[clients_no].socket = newsock_tcp;
                        strcpy(clients[clients_no].id, tcp_aux.payload);
                        clients[clients_no].online = 1;
                        clients_no++;
                        printf("New client %s connected from %s:%d.\n", clients[clients_no - 1].id,
                               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

                    } else {
                        /*clientul exista si este online cand se incearca conectarea*/
                        if (clients[client_index].online == 1) {
                            printf("Client %s already connected.\n", tcp_aux.payload);

                            struct client_tcp close_connection;
                            strcpy(close_connection.payload, "exit");
                            send(newsock_tcp, (char *) &close_connection, sizeof(struct client_tcp), 0);
                            send(clients[client_index].socket, (char *) &close_connection, sizeof(struct client_tcp),0);
                            clients[client_index].online = 0;

                        } else {
                            clients[client_index].online == 1;
                            clients[client_index].socket == newsock_tcp;
                            printf("New client %s connected from %s:%d.\n", clients[client_index].id,
                                   inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
                            /* se trimit mesajele bufferate cat a fost deconectat (daca a ales optiunea si are mesaje*/
                        }
                    }
                } else if (i == sock_udp) {
                    n = recvfrom(sock_udp, &buffer, 1600, 0, (struct sockaddr *) &udp_addr, &udplen);
                    DIE(n < 0, "recv");
                    struct client_tcp send_tcp;
                    memset(&send_tcp, 0, sizeof(send_tcp));
                    send_tcp.udp_port = ntohs(udp_addr.sin_port);
                    strcpy(send_tcp.ip, inet_ntoa(udp_addr.sin_addr));
                    char topic[50];
                    strncpy(topic, buffer, 49);
                    strcpy(send_tcp.topic, topic);
                    int data_type = (uint8_t) buffer[50];

                    int topic_index = topic_already_registered(topics, topics_no, msg_udp.topic);

                    /*daca topicul nu exista, se creeaza*/
                    if (topic_index == -1) {
                        strcpy(topics[topics_no].name, msg_udp.topic);
                        topics[topics_no].type = msg_udp.data_type;
                        topics[topics_no].subs_no = 0;
                        
                        topics_no++;
                    } else {
                        /*decodificarea mesajului pentru a fi trimis sau bufferat*/
                        if (data_type == 0) {
                            strcpy(send_tcp.data_type, "INT");
                            long aux = ntohl(*(uint32_t * )(buffer + 52));
                            if (buffer[51] == 1) {
                                aux *= -1;
                            }

                            sprintf(send_tcp.payload, "%ld", aux);
                        }

                        if (data_type == 1) {
                            strcpy(send_tcp.data_type, "SHORT_REAL");
                            float aux = ntohs(*(uint16_t * )(buffer + 51));
                            aux /= 100.0;
                            sprintf(send_tcp.payload, "%.2f", aux);
                        }

                        if (data_type == 2) {
                            strcpy(send_tcp.data_type, "FLOAT");
                            double aux = ntohl(*(uint32_t * )(buffer + 52));
                            double result;
                            double exponent = buffer[56];
                            for (exponent; exponent > 0; exponent--) {
                                result = result * 10;
                            }
                            aux /= result;
                            if (buffer[51] == 1) {
                                aux *= -1;
                            }
                            sprintf(send_tcp.payload, "%lf", aux);
                        }

                        if (data_type == 3) {
                            strcpy(send_tcp.data_type, "STRING");
                            strcpy(send_tcp.payload, buffer + 51);
                        }

                        for (int j = 0; j < topics[topic_index].subs_no; j++) {
                            if (topics[topic_index].subscribers[j].online == 1 &&
                                topics[topic_index].subscribers[j].subscribed == 1) {
                                for (int k = 0; k < clients_no; k++) {
                                    if (strcmp(clients[k].id, topics[topic_index].subscribers[j].id) == 0)
                                        n = send(topics[topic_index].subscribers[j].socket, &send_tcp,
                                                 sizeof(struct client_tcp), 0);
                                    DIE(n < 0, "send");
                                }

                                /*daca clientul nu este online si este abonat la topic (sf 1), se va buffera mesajul pentru a fi
                                trimis cand se reconecteaza*/
                            } else {
                                if (topics[topic_index].subscribers[j].sf == 1) {
                                    /* daca clientul este abonat la topic si are sf activat, se pun mesajele in buffer*/
                                    for (int k = 0; k < buffered_msg_no; k++) {
                                        if (strcmp(buffered_msg[k].client_id, topics[topic_index].subscribers[j].id) ==0) {
                                            int msg_index = buffered_msg[k].msg_no;
                                            buffered_msg[k].buffered_msg[msg_index] = send_tcp;
                                            buffered_msg[k].msg_no++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                
                } else {
                    /* s-a primit mesaj de la unul din clientii conectati*/
                    struct msg_tcp tcp_received;
                    n = recv(i, (char *) &tcp_received, sizeof(struct msg_tcp), 0);
                    DIE(n < 0, "recv");
                    int sockt;
                    int index;
                    int topic_index;
                    int topic_subs;

                    if (n == 0) {
                        /* conexiunea s-a inchis*/
                        printf("Client %s disconnected.\n", clients[index].id);
                        clients[index].online = 0;
                        /* se scoate din multimea de citire socketul inchis*/
                        FD_CLR(i, &read_fds);
                    }
                        /*cauta daca exista topicul; daca da, returneaza indexul si numarul de subscriberi ai acestuia*/
                        for (int j = 0; j < topics_no; j++) {
                            if (strcmp(tcp_received.payload, topics[j].name) == 0) {
                                topic_index = j;
                                topic_subs = topics[j].subs_no;
                                break;
                            }
                        }
                        /*cauta in lista de clienti pentru a gasi clientul caruia ii apartine socketul pe care
                        s-a realizat cererea*/
                        for (int j = 0; j < clients_no; j++) {
                            if (clients[j].socket == i) {
                                sockt = clients[j].socket;
                                index = j;
                                break;
                            }
                        }
                        /*subscribe: se aboneaza clientul[index] la topicul din payload*/
                        if (tcp_received.action_type == 0) {
                            int check = topic_already_registered(topics, topics_no, tcp_received.payload);
                            if (check == -1) {
                                strcpy(topics[topics_no].name, tcp_received.payload);
                                topics[topics_no].subs_no = 1;
                                topics[topics_no].subscribers[0] = clients[index];
                                topics[topics_no].subscribers[0].sf = tcp_received.sf;
                                topics[topics_no].subscribers[0].subscribed = 1;
                                topics_no++;
                            } else {
                                strcpy(topics[check].subscribers[topic_subs].id, clients[index].id);
                                topics[check].subscribers[topic_subs].sf = tcp_received.sf;
                                topics[check].subscribers[topic_subs].subscribed = 1;
                                topics[check].subs_no++;
                            }
                        }
                        /*unsubscribe*/
                        if (tcp_received.action_type == 1) {
                            topics[topic_index].subscribers[topic_subs].subscribed = 0;

                        }
                    }
                }
            }
        }
    
    
    free(clients);
    free(topics);
    free(buffered_msg);

    end:
    for (int i = 2; i <= fdmax; i++) {
        if (FD_ISSET(i, &read_fds)) {
            close(i);
        }
    }
    return 0;
}