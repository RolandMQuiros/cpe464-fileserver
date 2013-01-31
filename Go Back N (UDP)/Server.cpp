#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sstream>

extern "C" {
    #include "select_call.h"
}
#include "cpe464.h"

#include "Server.h"
#include "Exception.h"

#define CXN_THRESH 100
//~ #define DEBUG_CHLD

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

Server::Server(float errorPercent) :
mvErrorPercent(errorPercent),
mvFrom(0),
mvSequence(0),
mvRetries(PKT_TRNSMAX),
mvInitialized(false) {
    // Get socket
    sockaddr_in local;
    socklen_t len;
    
    if ((mvSocket = GetSocket(local, len)) == -1) {
        throw Exception(__LINE__, "GetSocket: ", strerror(errno));
    }
    
    // Print port number
    std::cout << "Socket created on Port " << ntohs(local.sin_port)
              << std::endl;
    
    // Configure signaling for forking
    struct sigaction sa;
    int t;

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        throw Exception(__LINE__, "sigaction", strerror(errno));
    }
}

Server::~Server()
{
    close(mvSocket);
    if (mvFrom != 0) {
        close(mvFrom);
    }
}

int Server::GetSocket(sockaddr_in &local, socklen_t &len) {
    int sk;
    if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return -1;
    }
    
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0); // Let system choose port
    
    // Bind the name to a port
    if (bind(sk, (sockaddr *)&local, sizeof(local)) < 0) {
        return -1;
    }
    
    // get port name
    len = sizeof(local);
    if (getsockname(sk, (sockaddr *)&local, &len) < 0) {
        return -1;
    }
    
    return sk;
}

int Server::Run() {
    packet outpkt;
    packet inpkt;
    
    sockaddr_storage theirAddr;
    socklen_t addrLen = sizeof(sockaddr_storage);
    
    bool cxn2 = false;
    bool sending;
    int sk;
    sockaddr_in local;
    socklen_t len;
        
    std::cout << "Awaiting connections..." << std::endl;

    while (1) {
        sending = false;
        if (recvfrom(mvSocket, &inpkt, sizeof(packet), 0,
                     (sockaddr *)&theirAddr, &addrLen) == -1) {
            std::cerr << "recvfrom (" << __LINE__ << "): "<< strerror(errno)
                      << std::endl;
            return 1;
        }
        
        unsigned short ck = inpkt.checksum;
        inpkt.checksum = 0;

        if (ck != in_cksum((unsigned short *)&inpkt, sizeof(packet))) {
            continue;
        }
        
        switch (inpkt.type) {
        case PKT_TYPE_CXN:
            char str[INET_ADDRSTRLEN];
            int pid;
            
            // New connection found
            std::cout << "Connection received from "
                      << inet_ntop(theirAddr.ss_family,
                                   (void *)&(((struct sockaddr_in*)&theirAddr)->
                                             sin_addr),
                                   str, sizeof(str)) << std::endl;
            
            // Set up a new socket for child process                
            sk = GetSocket(local, len);
            
            // Send RR for connection
            packet rr;
            memset(&rr, PKT_TYPE_RR, sizeof(packet));
            rr.sequence = inpkt.sequence;
            rr.size = htons(local.sin_port);
            rr.checksum = 0;
            rr.checksum = in_cksum((unsigned short *)&rr, sizeof(packet));
            outpkt = rr;
            sending = true;
            cxn2 = true;
            break;
        case PKT_TYPE_CXN2:
            if (!cxn2) { break; }
            cxn2 = false;
            // Send RR for connection stage 2
            outpkt = rrpkt(inpkt.sequence);
            sending = true;
            
            // Create child process
            if ((pid = fork()) == 0) {                
                #ifdef DEBUG_CHLD
                    select_call(-1, 10, 0); // Gives us some time to gdb the
                                            // child process
                #endif
                close(mvSocket);    // Close parent socket
                mvSocket = sk;
                mvAddr = theirAddr;
                mvAddrLen = sizeof(mvAddr);
                mvSequence = 1;
                mvPort = local.sin_port;
                mvState = INIT;
                
                Child();
            } else if (pid > 0) {
                std::cout << "Starting new process [" << pid << "]"
                          << std::endl;
            }
            break;
        case PKT_TYPE_REJ:
            sending = true;
            break;
        }
        
        if (sending) {
            if (sendtoErr(mvSocket, &outpkt, sizeof(packet), 0,
                          (sockaddr *)&theirAddr, addrLen) == -1) {
                std::cerr << "open (" << __LINE__ << "): " << strerror(errno);
                return 1;
            }
        }
    }
}

inline int Server::Child() {
    packet buf;
    
    // Main child loop
    while (mvState != DONE && mvState != ERROR && mvRetries > 0) {
        switch (mvState) {
        case INIT:
            mvState = init();
            break;
        case FILL_WINDOW:
            mvState = fillWindow();
            break;
        case SEND_WINDOW:
            mvState = sendWindow();
            break;
        case WAIT_RR:
            mvState = waitRR();
            break;
        }
    }
    
    if (mvState == ERROR) {
        std::cout << "Error receiving file.  Exiting." << std::endl;
        exit(1);
    }
    
    if (mvRetries <= 0) {
        std::cout << "Maximum number of retries reached.  Exiting."
                  << std::endl;
        exit(1);
    }
    
    std::cout << "File transfer successful.  Exiting." << std::endl;
    exit(0);
}

int Server::recvPacket(packet &buf) {
    #ifndef DEBUG_CHLD
        // Check for timeout
        if (select_call(mvSocket, 1, 0) <= 0) {
            std::cerr << "Client timed out.  Retries left: " << mvRetries--
                      << std::endl;
            return 2;
        }
        mvRetries = PKT_TRNSMAX;
    #endif

    // Receive packets
    if (recvfrom(mvSocket, &buf, sizeof(packet), 0,
                 (sockaddr *)&mvAddr, &mvAddrLen) == -1) {
        std::cerr << "recvfrom (" << __LINE__ << "): " << strerror(errno);
        return 1;
    }
    
    // Verify packet checksum
    uint16_t ck = buf.checksum;
    buf.checksum = 0;

    if ((buf.checksum = in_cksum((unsigned short*)&buf, sizeof(packet)))
        != ck) {
        std::cerr << "Received packet with bad checksum.  Expected 0x"
                  << std::hex << ck << ", received 0x" << std::hex
                  << buf.checksum << std::endl
                  << "Recv Sequence: " << std::dec << buf.sequence << std::endl
                  << "Retries left: " << std::dec << mvRetries-- << std::endl;
        return 2;
    }
    
    return 0;
}

Server::State Server::init() {
    packet inpkt;
    packet outpkt;
    
    bool winszSet = false;
    bool bufszSet = false;
    bool fnSet = false;
    
    // Wait for initialization packets
    while ((!winszSet || !bufszSet || !fnSet) && mvRetries > 0) {
        int r;
        switch (recvPacket(inpkt)) {
        case 1:
            // Receive failed.  Terminate.
            return ERROR;
        case 2:
            // Bad checksum or timeout.  Send a Reject.
            outpkt = rejpkt(mvSequence);
            break;
        default:
            // If proper sequence, send RR.  Otherwise, send reject.
            if (inpkt.sequence <= mvSequence) {
                mvRetries = PKT_TRNSMAX;
                outpkt = rrpkt(mvSequence);
                
                // Process packet
                if (inpkt.sequence == mvSequence) {
                    mvSequence++;
                    switch (inpkt.type) {
                    case PKT_TYPE_BUF:
                        mvBufferSize = inpkt.size;
                        bufszSet = true;
                        break;
                    case PKT_TYPE_WIN:
                        mvWindowSize = inpkt.size;
                        winszSet = true;
                        break;
                    case PKT_TYPE_FLN:
                        mvFromName = (char *)inpkt.data;
                        fnSet = true;
                        break;
                    }
                }
            } else {
                std::cerr << "Received initialization packet with incorrect "
                             "sequence.  Expected " << mvSequence << ", got "
                          << inpkt.sequence << std::endl;
                mvRetries--;
                outpkt = rejpkt(mvSequence);
            }
            break;
        }
        
        // Send REJ or RR
        if (sendtoErr(mvSocket, &outpkt, sizeof(packet), 0, (sockaddr *)&mvAddr,
                      mvAddrLen) == -1) {
            std::cerr << "sendto (" << __LINE__ << "): " << strerror(errno);
            return ERROR;
        }
    }
    
    if (mvRetries <= 0) {
        return ERROR;
    }
    
    // Open file
    if ((mvFrom = open(mvFromName.c_str(), O_RDONLY)) == -1) {
        std::cerr << "open (" << __LINE__ << "): " << strerror(errno);
        return ERROR;
    }       
    
    // Reset our values for sliding window
    mvSequence = 0;
    mvRetries = PKT_TRNSMAX;
    
    return FILL_WINDOW;
}

Server::State Server::fillWindow() {
    // Read packets from file to fill the window
    while (mvWindow.size() < mvWindowSize) {
        packet buf = { 0 };
        int rd;
        if ((rd = read(mvFrom, buf.data, mvBufferSize)) < 0) {
            // Read error.  Can't do anything about this.
            std::cerr << "read (" << __LINE__ << "): " << strerror(errno);
            return ERROR;
        }
        
        buf.type = PKT_TYPE_DAT;
        buf.sequence = mvSequence++;
        buf.size = rd;
        buf.checksum = in_cksum((unsigned short *)&buf, sizeof(packet));
        
        mvWindow.push_back(buf);
        mvOutBuf.push_back(buf);
    }
    
    return SEND_WINDOW;
}

Server::State Server::sendWindow() {    
    while (!mvOutBuf.empty()) {
        if (sendtoErr(mvSocket, &mvOutBuf.front(), sizeof(packet), 0,
                      (sockaddr *)&mvAddr, mvAddrLen) == -1) {
            std::cerr << "sendto " << __LINE__ << ": " << strerror(errno);
            return ERROR;
        }
        mvOutBuf.pop_front();
    }
    
    return WAIT_RR;
}

Server::State Server::waitRR() {
    packet buf;
    
    switch (recvPacket(buf)) {
    case 1:
        return ERROR;
    case 2:
        return FILL_WINDOW;
    }
    
    switch (buf.type) {
    case PKT_TYPE_RR:
        if (buf.sequence >= mvWindow.front().sequence) {
            // If we receive RRs for our expected sequence or greater, shift
            // the window.  If the RR is greater, then we can assume that
            // previous RRs were sent, but were lost in transit.  We'll
            // simply shift the window over the distance.
            for (unsigned int i = mvWindow.front().sequence;
                 !mvWindow.empty() && i <= buf.sequence; i++)
            {
                mvWindow.pop_front();
            }
            
            // Reset our retry counter
            mvRetries = PKT_TRNSMAX;
            
            return FILL_WINDOW;
        }
        // We ignore older RRs
        break;
    case PKT_TYPE_REJ:
        // If we receive REJ with our expected sequence or greater,
        // we resend the whole window.  Client should re-send old RRs if our
        // sequence is lower than its own.
        if (buf.sequence >= mvWindow.front().sequence) {
            std::cerr << "Received REJ" << buf.sequence
                      << ".  Window sequence: " << mvWindow.front().sequence
                      //~ << ".  Retries left: " << mvRetries--
                      << std::endl;
            mvOutBuf = mvWindow;
            return FILL_WINDOW;
        } else {
            // If we receive a REJ for a lower sequence, we'll need to
            // rewind our window to an earlier point in the file
            mvSequence = buf.sequence;
            if (lseek(mvFrom, mvBufferSize * mvSequence, SEEK_SET) == -1) {
                std::cerr << "lseek (" << __LINE__ << "): " << strerror(errno);
                return ERROR;
            }
            mvOutBuf.clear();
            mvWindow.clear();
            return FILL_WINDOW;
        }
        break;
    }
    
    return WAIT_RR;
}
