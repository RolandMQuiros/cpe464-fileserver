#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
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

#include "select_call.h"
#include "packet.h"
#include "cpe464.h"

#define NUM_ARGS 7
#define ARG_FROM 1
#define ARG_TO 2
#define ARG_BUFSZ 3
#define ARG_PERR 4
#define ARG_REMNAME 5
#define ARG_REMPORT 6

#define TIMEOUT_SEC 11
#define TIMEOUT_USEC 0

enum state_t {
    TRANSMIT,
    RECEIVE,
    EVALUATE,
    QUIT
};

char *g_appname;

/**Creates and connects a socket to the first remote machine found.
 * @param servinfo server address info
 * @return socket file descriptor if found, -1 otherwise
 */
int findsocket(struct addrinfo *servinfo, struct addrinfo **conninfo) {
    struct addrinfo *iter;
    int sockfd;
    
    for (iter = servinfo; iter != NULL; iter = iter->ai_next) {
        /* create socket */
        if ((sockfd = socket(iter->ai_family, iter->ai_socktype,
                             iter->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        
        /* connect socket to address */
        if (connect(sockfd, iter->ai_addr, iter->ai_addrlen) == -1) {
            close(sockfd);
            perror("connect");
            continue;
        }
        
        break;
    }
    
    if (conninfo != NULL) {
        *conninfo = iter;
    }
    
    return (iter == NULL) ? -1 : sockfd;
}

int connect_to_server(const char *servname, const char *port,
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
    if ((tmp = findsocket(server, &conn)) == -1) {
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

void printFlags(uint8_t flags) {
    printf("Status flags:\n"
           "\tQuit: %s\n"
           "\tExpect Ack: %s\n"
           "\tTransmit: %s\n"
           "\tReceive: %s\n"
           "\tEnd: %s\n",
           (flags & (1 << QUIT)) ? "yes" : "no",
           (flags & (1 << EXPECT_ACK)) ? "yes" : "no",
           (flags & (1 << TRANSMIT)) ? "yes" : "no",
           (flags & (1 << RECEIVE)) ? "yes" : "no",
           (flags & (1 << END)) ? "yes" : "no");
}

int main(int argc, char *argv[]) {
    int sockfd;                   /* socket descriptor */
    int tofd;                     /* file descriptor for target file */
    struct addrinfo hints;        /* server search hints */
    struct packet outpkt = { 0 }; /* outgoing packet */
    struct packet inpkt = { 0 };  /* incoming packet */
    struct packet tpkt = { 0 };
    uint16_t bufsz;               /* bytes per packet */
    float perr;                   /* percent error */
    
    uint32_t sequence = 0; /* current expected sequence number */
    uint16_t checksum = 0;
    uint16_t ck = 0;
    int retries = 0;           /* retransmit count */
    state_t state;             /* state machine state */
    
    /* check arguments */
    if (argc != NUM_ARGS) {
        fprintf(stderr, "usage: %s from-remote-file to-local-file buffer-size "
                "error-percent remote-machine remote-port\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    /* initialize corruption */
    sendErr_init(atof(argv[ARG_PERR]), DROP_OFF, FLIP_ON, DEBUG_ON, RSEED_OFF);
    
    g_appname = argv[0];
    bufsz = atoi(argv[ARG_BUFSZ]);
    perr = atof(argv[ARG_PERR]);
    
    /* build socket hints */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       /* IPv4 */
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    
    /* connect to server */
    if ((sockfd = connect_to_server(argv[ARG_REMNAME], argv[ARG_REMPORT],
                                   &hints)) == -1)
    {
        return EXIT_FAILURE;
    }
    
    /* open local target file */
    if ((tofd = creat(argv[ARG_TO], S_IRWXU)) == -1) {
        perror("creat");
        return EXIT_FAILURE;
    }
    
    /* request a file from server */
    outpkt.type = PKT_TYPE_FLN;
    memcpy(outpkt.data, argv[ARG_FROM], strlen(argv[ARG_FROM]));
    outpkt.size = bufsz;
    outpkt.checksum = in_cksum((unsigned short *)&outpkt, sizeof(outpkt));
    
    int i = 0;
    
    while (state != QUIT) {
        printf("iteration %d\n", i++);
        printf("retries %d\n", retries);
        printf("sequence %d\n", sequence);
        printFlags(status);
        
        if (state == TRANSMIT) {
            sendpacket(sockfd, &outpkt, sizeof(outpkt), 0);
            state = UPDATE;
        } e
        
        /* check if we've maxed out number of retries */
        if (retries >= PKT_TRNSMAX) {
            close(tofd);
            fprintf(stderr, "%s: server unreachable\n", argv[0]);
            break;
        }
        
        /* transmit a packet if needed */
        if (status & (1 << TRANSMIT)) {
            /* Keep trying until send succeeds, up to PKT_DTRNSMAX times */
            if (sendErr(sockfd, &outpkt, sizeof(outpkt), 0) == -1 &&
                retries < PKT_TRNSMAX)
            {
                perror("send");
                fprintf(stderr, "%s: retries remaining: %d", argv[0], retries);
            }
            
            /* disable transmit flag */
            status &= ~(1 << TRANSMIT);
            
            /* expect an ack on next packet received */
            if (outpkt.type != PKT_TYPE_ACK) {
                status |= 1 << RECEIVE;
                if (outpkt.type != PKT_TYPE_RET) {
                    status |= 1 << EXPECT_ACK;
                }
            } else if (status & (1 << END)) {
                status |= 1 << QUIT;
                continue;
            }
        }
        
        //~ /* wait for incoming packets.  disable this while debugging. */
        //~ if (select_call(sockfd, TIMEOUT_SEC, TIMEOUT_USEC) == 0) {
            //~ /* exit on timeout */
            //~ close(tofd);
            //~ fprintf(stderr, "%s: connection to server timed out\n", argv[0]);
            //~ return EXIT_FAILURE;
        //~ }
        
        /* retrieve packet */
        if (recv(sockfd, &inpkt, sizeof(inpkt), 0) == -1) {
            /* can't recover from this, so exit */
            close(tofd);
            return EXIT_FAILURE;
        }
        
        /* verify checksum */
        checksum = inpkt.checksum;
        inpkt.checksum = 0;
        if ((ck = in_cksum((unsigned short *)&inpkt, sizeof(inpkt))) != checksum)
        {
            /* request retransmission of packet with current sequence number */
            if (outpkt.type != PKT_TYPE_RET) {
                tpkt = outpkt;
            }
            outpkt = retransmitpkt(sequence);
            status |= 1 << TRANSMIT;
            fprintf(stderr, "%s: bad checksum.  Requesting retransmission of "
                    "packet with sequence number %d\n", argv[0], sequence);
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
                    /* acknowledgment packets are identical to the data packet,
                       except the type */
                    if (memcmp(outpkt.data, inpkt.data,
                               sizeof(outpkt.data)) == 0) {
                        /* acknowledgment accepted */
                        status &= ~(1 << EXPECT_ACK);
                        retries = 0;
                    }
                } else {
                    /* exit if we retrieve an unexpected acknowledgment */
                    fprintf(stderr, "%s: unexpected acknowledgment. "
                            "How embarrassing.\n", argv[0]);
                }
                break;
            case PKT_TYPE_RET:
                /* If we retrieve a retransmission request, we'll just let the
                   loop try again. We'll increment our retry counter, as well.*/
                status |= 1 << TRANSMIT;
                retries++;
                continue;
                break;
            case PKT_TYPE_DAT:
                /* if we retrieve a valid data packet, add data to file */
                if (htonl(sequence) == inpkt.sequence) {
                    if (write(tofd, inpkt.data, inpkt.size) == -1) {
                        perror("write");
                        status |= 1 << QUIT;
                    }
                    
                    if (inpkt.size < bufsz) {
                        status |= 1 << END;
                    }
                    
                    /* increment sequence */
                    sequence++;
                    
                    /* prepare an acknowledgment */
                    outpkt = ackpkt(&inpkt);
                } else {
                    /* if we receive the wrong sequence, transmit a sequence
                       request */
                    outpkt = retransmitpkt(sequence);
                    fprintf(stderr, "%s: packet with wrong sequence number "
                            "received (%d).  Requesting retransmission.\n",
                            argv[0], sequence);
                }
                status |= 1 << TRANSMIT;
                break;
        }
    }
    
    close(tofd);
    close(sockfd);
    
    return EXIT_SUCCESS;
}