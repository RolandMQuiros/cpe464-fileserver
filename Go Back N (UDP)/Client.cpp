#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

extern "C" {
    #include "select_call.h"
}

#include "cpe464.h"

#include "Client.h"
#include "Exception.h"

//~ #define DEBUG_CHLD

Client::Client(const std::string &from, const std::string &to,
               unsigned int bufferSize, float errorPercent,
               unsigned int windowSize, const std::string &remoteMachine,
               const std::string &remotePort) :
mvFromName(from),
mvToName(to),
mvBufferSize(bufferSize),
mvErrorPercent(errorPercent),
mvWindowSize(windowSize),
mvRemoteMachine(remoteMachine),
mvRemotePort(atoi(remotePort.c_str())),
mvRetries(PKT_TRNSMAX),
mvSequence(0),
mvState(INIT) {
    // Get socket
    if ((mvSocket = GetSocket(*(sockaddr_in *)&mvAddr)) == -1) {
        throw Exception(__LINE__, "GetSocket: ", strerror(errno));
    }
    mvAddrLen = sizeof(mvAddr);
    
    // Open local target file
    if ((mvTo = creat(mvToName.c_str(), S_IRWXU)) == -1) {
        throw Exception(__LINE__, "creat", strerror(errno));
    }
}

Client::~Client() {
    close(mvTo);
    close(mvSocket);
    close(mvOldSocket);
}

int Client::GetSocket(sockaddr_in &remote) {
    int sk;
    hostent *hp;
    
    if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return -1;
    }
    
    remote.sin_family = AF_INET;
    hp = gethostbyname(mvRemoteMachine.c_str());
    memcpy(&remote.sin_addr, hp->h_addr, hp->h_length);
    
    remote.sin_port = htons(mvRemotePort);
    
    return sk;
}

int Client::Run() {
    while (mvState != DONE && mvState != ERROR && mvRetries > 0) {
        switch (mvState) {
            case INIT:
                mvState = init();
                break;
            case RECV_PACKETS:
                mvState = recvPackets();
                break;
            default:
            break;
        }
    }

    if (mvState == ERROR) {
        std::cout << "Error receiving file.  Exiting." << std::endl;
        return 1;
    }
    
    if (mvRetries <= 0) {
        std::cout << "Maximum number of retries reached.  Exiting."
                  << std::endl;
        return 1;
    }
    
    std::cout << "File transfer successful.  Exiting." << std::endl;
    return 0;
}

int Client::recvPacket(packet &buf) {
    #ifndef DEBUG_CHLD
        // Check for timeout
        if (select_call(mvSocket, 1, 0) == 0) {
            std::cerr << "Server timed out.  Retries left: " << mvRetries--
                      << std::endl;
            return 2;
        }
        
        mvRetries = PKT_TRNSMAX;
    #endif

    // Receive packets
    if (recvfrom(mvSocket, &buf, sizeof(packet), 0,
                 (sockaddr *)&mvAddr, &mvAddrLen) == -1) {
        std::cerr << "recvfrom (" << __LINE__ << "): " << strerror(errno)
                  << std::endl;
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
                  << "Sequence: " << std::dec << buf.sequence << std::endl
                  << "Retries left: " << std::dec << mvRetries-- << std::endl;
        return 2;
    }
    
    return 0;
}

int Client::writeTo(packet &in) {
    if (write(mvTo, in.data, in.size) == -1) {
        std::cerr << "write (" << __LINE__ << "): " << strerror(errno)
                  << std::endl;
        return 1;
    }
    return 0;
}

Client::State Client::init() {
    // Build packets
    packet pkt[5];
    
    // connection packet
    memset(&pkt[0], PKT_TYPE_CXN, sizeof(packet));
    pkt[0].checksum = 0;
    pkt[0].sequence = 0;
    pkt[0].checksum = in_cksum((unsigned short *)&pkt[0], sizeof(packet));
    
    // 2nd stage connection response
    memset(&pkt[1], PKT_TYPE_CXN2, sizeof(packet));
    pkt[1].sequence = 1;
    pkt[1].checksum = 0;
    pkt[1].checksum = in_cksum((unsigned short *)&pkt[1], sizeof(packet));
    
    // buffer size packet
    memset(&pkt[2], PKT_TYPE_BUF, sizeof(packet));
    pkt[2].size = mvBufferSize;
    pkt[2].sequence = 2;
    pkt[2].checksum = 0;
    pkt[2].checksum = in_cksum((unsigned short *)&pkt[2], sizeof(packet));

    // window size packet
    memset(&pkt[3], PKT_TYPE_WIN, sizeof(packet));
    pkt[3].size = mvWindowSize;
    pkt[3].sequence = 3;
    pkt[3].checksum = 0;
    pkt[3].checksum = in_cksum((unsigned short *)&pkt[3], sizeof(packet));

    // file name packet
    memset(&pkt[4], PKT_TYPE_FLN, sizeof(packet));
    memcpy(pkt[4].data, mvFromName.c_str(), mvFromName.length()+1);
    pkt[4].data[mvFromName.length()] = '\0';
    pkt[4].sequence = 4;
    pkt[4].checksum = 0;
    pkt[4].checksum = in_cksum((unsigned short *)&pkt[4], sizeof(packet));
    
    // Send packets
    int sk; // new socket
    mvOldSocket = mvSocket;
    sockaddr_storage addr;
    for (int i = 0; i < 5 && mvRetries > 0; i++) {
        packet inpkt;
        int r;
        
        // Send packet
        if (sendtoErr(mvSocket, &pkt[i], sizeof(packet), 0, (sockaddr *)&mvAddr,
                      mvAddrLen) == -1) {
            std::cerr << "sendto (" << __LINE__ << "): " << strerror(errno)
                      << std::endl;
            return ERROR;
        }
        
        // Receive packet
        if ((r = recvPacket(inpkt)) == 1) {
            return ERROR;
        } else if (r == 2) {
            i--;
            continue;
        }
        mvRetries = PKT_TRNSMAX;
        
        // Check for RRs
        switch (inpkt.type) {
        case PKT_TYPE_RR:
            if (inpkt.sequence == i && pkt[i].type == PKT_TYPE_CXN) {
                mvRemotePort = inpkt.size; // Extract new port number
                if ((sk = GetSocket(*(sockaddr_in *)&addr)) == -1) {
                    std::cerr << "GetSocket (" << __LINE__ << "): "
                              << strerror(errno) << std::endl;
                    return ERROR;
                }
                mvAddrLen = sizeof(mvAddr);
            } else if (inpkt.sequence == i && pkt[i].type == PKT_TYPE_CXN2) {
                // Apply new port changes
                mvSocket = sk;
                mvAddr = addr;
            } else if (inpkt.sequence != i) {
                // Incorrect sequence number: resend packet
                i--;
                continue;
            }
            break;
        case PKT_TYPE_REJ:
            if (inpkt.sequence <= i) {
                i = inpkt.sequence - 1;
            }
            break;
        }
    }
    
    if (mvRetries <= 0) {
        return ERROR;
    }
    
    mvSequence = 0;
    mvRetries = PKT_TRNSMAX;
    
    return RECV_PACKETS;
}

Client::State Client::recvPackets() {
    packet inpkt;
    packet outpkt;

    // Check for timeout
    switch (recvPacket(inpkt)) {
    case 1:
        // Receive error.
        return ERROR;
    case 2:
        // Bad checksum or timeout.
        outpkt = rejpkt(mvSequence);
        break;
    default:
        // if sequence is less than or equal to our own, send RR.  Even if it's
        // lower than it's supposed to be, we'll just send the RR to make the
        // server feel better about itself.
        if (inpkt.sequence <= mvSequence) {
            mvRetries = PKT_TRNSMAX;
            outpkt = rrpkt(inpkt.sequence);
            
            if (inpkt.sequence == mvSequence) {
                mvSequence++;
                
                if (writeTo(inpkt) == 1) {
                    return ERROR;
                }
                
                if (inpkt.type == PKT_TYPE_DAT && inpkt.size < mvBufferSize) {
                    // A valid, less-than-maximum sized packet indicates
                    // end-of-file
                    return DONE;
                }
            }
        } else {
            // if the sequence is outright wrong, however...
            std::cout << "Received packet with incorrect sequence.  Expected "
                      << mvSequence << " or lower.  Received " << inpkt.sequence
                      << std::endl;
            outpkt = rejpkt(mvSequence);
        }
    }
    
    // Send response packet
    if (sendtoErr(mvSocket, &outpkt, sizeof(packet), 0, (sockaddr *)&mvAddr,
                  mvAddrLen) == -1)
    {
        std::cerr << "sendto (" << __LINE__ << "): " << strerror(errno)
                  << std::endl;
        return ERROR;
    }
    
    return RECV_PACKETS;
}

