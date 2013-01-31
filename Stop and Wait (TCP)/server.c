#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "select_call.h"
#include "packet.h"
#include "cpe464.h"

#define TIMEOUT_SEC 11
#define TIMEOUT_USEC 0

#define QUIT 0
#define EXPECT_ACK 1
#define EXPECT_INFO 2
#define INFO_INIT 3
#define TRANSMIT 4
#define RECEIVE 5
#define NEXTDATA 6
#define RETRANSMIT 7

char *g_appname;

void sigchldhandler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0) { }
}

/**Creates and connects a socket to the first remote machine found.
 * @param servinfo server address info
 * @return socket file descriptor if found, -1 otherwise
 */
int findsocket(struct addrinfo *servinfo, struct addrinfo **conninfo) {
    struct addrinfo *iter;
    int sockfd;
    int yes = 1;
    
    for (iter = servinfo; iter != NULL; iter = iter->ai_next) {
        /* create socket */
        if ((sockfd = socket(iter->ai_family, iter->ai_socktype,
                             iter->ai_protocol)) == -1) {
            perror("findsocket: socket");
            continue;
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("findsocket: setsockopt");
            return -1;
        }
        
        /* bind socket to address */
        if (bind(sockfd, iter->ai_addr, iter->ai_addrlen) == -1) {
            /* bad bind, keep going until we find a good one */
            close(sockfd);
            perror("findsocket: connect");
            continue;
        }
        
        break;
    }
    
    if (conninfo != NULL) {
        *conninfo = iter;
    }
    
    return (iter == NULL) ? -1 : sockfd;
}

int connect_to_client(const char *servname, const char *port,
                      const struct addrinfo *hints)
{
    int tmp;
    struct addrinfo *server;
    struct addrinfo *conn;
    char servstr[INET_ADDRSTRLEN];
    
    /* get server address info */
    if ((tmp = getaddrinfo(servname, port, hints, &server)) != 0) {
        fprintf(stderr, "%s: connect_to_server: getaddrinfo: %s\n",
                g_appname, gai_strerror(tmp));
        return -1;
    }
    
    /* get socket */
    if ((tmp = findsocket(server, &conn)) == -1 || conn == NULL) {
        fprintf(stderr, "%s: connect_to_server: could not create a valid "
                "socket\n", g_appname);
        return -1;
    }
    
    /* print connection info */
    inet_ntop(conn->ai_family, conn->ai_addr, servstr, INET_ADDRSTRLEN);
    fprintf(stdout, "%s: connecting to server %s...\n", g_appname, servstr);
    
    /* release unneeded server info */
    freeaddrinfo(server);
    
    return tmp;
}

int file_to_packet(int filefd, unsigned int sequence, unsigned int bufsz,
                   struct packet *buf) {
    int rd = 0;
    
    if (buf != NULL) {
        if ((rd = read(filefd, buf->data, bufsz)) == -1) {
            perror("read");
            return -1;
        }
        
        buf->type = PKT_TYPE_DAT;
        buf->sequence = htonl(sequence);
        buf->size = rd;
        
        buf->checksum = 0;
        buf->checksum = in_cksum((unsigned short *)buf,
                                  sizeof(*buf));
    }    
    return rd;
}

void printFlags(uint8_t flags) {
    printf("Status flags:\n"
           "\tQuit: %s\n"
           "\tExpect Ack: %s\n"
           "\tExpect Info: %s\n"
           "\tInitialize file: %s\n"
           "\tTransmit: %s\n"
           "\tReceive: %s\n"
           "\tNext packet from file: %s\n"
           "\tRetransmit: %s\n",
           (flags & (1 << QUIT)) ? "yes" : "no",
           (flags & (1 << EXPECT_ACK)) ? "yes" : "no",
           (flags & (1 << EXPECT_INFO)) ? "yes" : "no",
           (flags & (1 << INFO_INIT)) ? "yes" : "no",
           (flags & (1 << TRANSMIT)) ? "yes" : "no",
           (flags & (1 << RECEIVE)) ? "yes" : "no",
           (flags & (1 << NEXTDATA)) ? "yes" : "no",
           (flags & (1 << RETRANSMIT)) ? "yes" : "no");
}

int client_comm(int connfd) {
    int fromfd;
    char *fromname = NULL;
    struct stat fromstat;
    unsigned int fromlen = 0;
    
    unsigned int bufsz = 0;
    struct packet outpkt = { 0 };
    struct packet inpkt = { 0 };
    struct packet tpkt = { 0 };
    
    uint8_t status = 0;
    uint32_t sequence = 0;
    uint16_t checksum = 0;
    unsigned short ck = 0;
    unsigned int retries = 0;
    ssize_t rd = 0;
    unsigned int offset = 0;
    
    /* set to receive buffer size packet */
    status |= 1 << RECEIVE;
    status |= 1 << EXPECT_INFO;
    
    int i = 0;
    
    while (!(status & (1 << QUIT))) {
        printf("iteration %d\n", i++);
        printf("retries %d\n", retries);
        printf("sequence %d\n", sequence);
        printFlags(status);
        
        /* check if we've maxed out number of retries */
        if (retries >= PKT_TRNSMAX) {
            close(fromfd);
            fprintf(stderr, "%s: client unreachable\n", g_appname);
            break;
        }
        
        /* transmit a packet if needed */
        if (status & (1 << TRANSMIT)) {
            /* Keep trying until send succeeds, up to PKT_DTRNSMAX times */
            if (sendErr(connfd, &outpkt, sizeof(outpkt), 0) == -1 &&
                   retries < PKT_TRNSMAX)
            {
                perror("send");
                fprintf(stderr, "client_comm: retries remaining: %d", retries);
            }
            
            /* disable transmit flag */
            status &= ~(1 << TRANSMIT);
            
            /* expect an ack on next packet received */
            if (outpkt.type != PKT_TYPE_ACK) {
                status |= 1 << RECEIVE;
                if (outpkt.type != PKT_TYPE_RET) {
                    status |= 1 << EXPECT_ACK;
                }
            } else {
                status &= ~(1 << RECEIVE);
            }
        }
        
        /* receive a packet if needed */
        if (status & (1 << RECEIVE)) {
            //~ if (select_call(connfd, TIMEOUT_SEC, TIMEOUT_USEC) == 0) {
                //~ fprintf(stderr, "client_conn: connection to client timed "
                        //~ "out\n");
                //~ return EXIT_FAILURE;
            //~ }
        
            if (recv(connfd, &inpkt, sizeof(inpkt), 0) == -1) {
                perror("recv");
                close(fromfd);
                return EXIT_FAILURE;
            }
            
            /* verify checksum */
            checksum = inpkt.checksum;
            inpkt.checksum = 0;
            if ((ck = in_cksum((unsigned short *)&inpkt,
                               sizeof(inpkt))) != checksum)
            {
                /* request retransmission of packet with current sequence
                   number */
                if (outpkt.type != PKT_TYPE_RET) {
                    tpkt = outpkt;
                }
                outpkt = retransmitpkt(sequence);
                status |= 1 << TRANSMIT;
                
                // break here
                fprintf(stderr, "client_comm: bad checksum.  Requesting "
                        "retransmission of packet with sequence number %d\n",
                        sequence);
                continue;
            }
            
            /* put our last outgoing packet back after sending a retransmit
               request */
            if (outpkt.type == PKT_TYPE_RET) {
                outpkt = tpkt;
            }
            
            /* evaluate packet */
            switch (inpkt.type) {
                case PKT_TYPE_ACK:
                    /* if we retrieve an acknowledgment and are expecting one */
                    if (status & (1 << EXPECT_ACK)) {
                        /* acknowledgment packets are identical to the data
                           packet, except the type */
                        if (memcmp(outpkt.data, inpkt.data,
                                   sizeof(inpkt.data)) == 0) {
                            /* acknowledgment accepted */
                            status &= ~((1 << EXPECT_ACK) | (1 << RECEIVE));
                            /* prepare next set of data */
                            status |= 1 << NEXTDATA;
                            
                            /* reset retransmission counter */
                            retries = 0;
                            /* increment sequence number */
                            if (outpkt.type == PKT_TYPE_DAT) {
                                sequence++;
                            }
                            
                            /* correct file descriptor after retransmission */
                            if (status & (1 << RETRANSMIT)) {
                                status &= ~(1 << RETRANSMIT);
                                if (lseek(fromfd, offset, SEEK_SET) == -1) {
                                    perror("lseek");
                                    return EXIT_FAILURE;
                                }
                            }
                        }
                    } else {
                        /* exit if we retrieve an unexpected acknowledgment */
                        fprintf(stderr, "client_conn: unexpected "
                                "acknowledgment.  How embarrassing.\n");
                        close(fromfd);
                        return EXIT_FAILURE;
                    }
                    break;
                case PKT_TYPE_RET:
                    fprintf(stderr, "client_conn: received retransmission "
                            "request for packet with sequence number %d\n",
                            ntohl(inpkt.sequence));
                    retries++;
                    if (ntohl(inpkt.sequence) == sequence) {
                        status |= 1 << TRANSMIT;
                        continue;
                    } else if (!(status & (1 << INFO_INIT))) {
                        if (lseek(fromfd, bufsz * ntohl(inpkt.sequence),
                            SEEK_SET) == -1) {
                            perror("lseek");
                            close(fromfd);
                            return EXIT_FAILURE;
                        }
                        status |= (1 << TRANSMIT) | (1 << RETRANSMIT) | (1 << NEXTDATA);
                    }
                    
                    break;
                case PKT_TYPE_FLN:
                    /* buffer size packet */
                    if (status & (1 << EXPECT_INFO)) {
                        int filenamelen = strlen((char *)inpkt.data);
                        
                        /* extract packet size and file name */
                        bufsz = inpkt.size;
                        fromname = (char *)malloc(filenamelen);
                        memcpy(fromname, (char *)inpkt.data, filenamelen);
                        
                        status &= ~(1 << EXPECT_INFO);
                        status |= 1 << INFO_INIT;
                        
                        /* prepare an acknowledgment */
                        outpkt = ackpkt(&inpkt);
                        status |= 1 << TRANSMIT;
                        continue;
                    }
                    break;
            }
        }
        
        /* handle files */
        if (!(status & (1 << EXPECT_INFO))) {
            /* open file when we receive the info packet from client */
            if (status & (1 << INFO_INIT)) {
                if ((fromfd = open(fromname, O_RDONLY)) == -1) {
                    perror("open");
                    return EXIT_FAILURE;
                }
                
                /* turn off the flag so we don't load again */
                status &= ~(1 << INFO_INIT);
                
                /* get file stats */
                if (fstat(fromfd, &fromstat) == -1) {
                    perror("fstat");
                    close(fromfd);
                    return EXIT_FAILURE;
                }
                /* get file length */
                fromlen = fromstat.st_size;
                
                /* prepare data packet from file */
                status |= 1 << NEXTDATA;
            }
            
            /* read data into packet */
            if (status & (1 << NEXTDATA)) {
                if ((rd = file_to_packet(fromfd, sequence,
                                         bufsz, &outpkt)) == -1) {
                    close(fromfd);
                    return EXIT_FAILURE;
                } else if (rd == 0) {
                    /* done with file */
                    status |= 1 << QUIT;
                }
                
                /* add to offset */
                if (!(status & (1 << RETRANSMIT))) {
                    offset += rd;
                }
                
                /* don't prepare next data until acknowledgment received */
                status &= ~((1 << NEXTDATA) | (1 << RECEIVE));
                
                /* prepare to transmit packet */
                status |= 1 << TRANSMIT;
            }
        }
    }
    
    if (fromname != NULL) {
        free(fromname);
    }
    
    close(fromfd);
    
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    int sockfd;                /* listening socket */
    int newfd;                 /* connection socket */
    
    struct addrinfo hints;
    struct sockaddr_storage peer;
    socklen_t addrsz;
    
    struct sigaction sa;
    char str[INET_ADDRSTRLEN];
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s percent-error\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    g_appname = argv[0];
    
    /* create hints */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    /* connect to client */
    if ((sockfd = connect_to_client(NULL, "1337", &hints)) == -1)
    {
        return EXIT_FAILURE;
    }
    
    /* listen on socket */
    if (listen(sockfd, PKT_DMAX) == -1) {
        perror("listen");
        close(sockfd);
        return EXIT_FAILURE;
    }
    
    /* reap dead processes */
    sa.sa_handler = sigchldhandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    
    printf("%s: awaiting connections...\n", argv[0]);
    
    while (1) {
        addrsz = sizeof(peer);
        
        /* accept connections from clients */
        if ((newfd = accept(sockfd, (struct sockaddr *)&peer,
                            &addrsz)) == -1) {
            perror("accept");
            continue;
        }
        
        /* print client info */
        inet_ntop(peer.ss_family, (struct sockaddr_in *)&peer, str,
                  sizeof(str));
        printf("%s: connection from %s\n", argv[0], str);
        
        /* each connection is its own process */
        //if (!fork()) {
        //    close(sockfd);
        /* initialize corruption */
        sendErr_init(atof(argv[1]), DROP_OFF, FLIP_ON, DEBUG_ON, RSEED_OFF);
        client_comm(newfd);
        //}
        close(newfd);
    }
    
    return EXIT_SUCCESS;
}