#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <deque>
#include <string>

extern "C" {
    #include "packet.h"
}

class Server {
public:
    Server(float errorPercent);
    ~Server();

    int GetSocket(sockaddr_in &local, socklen_t &len);
    
    int Run();
    int Child();
private:
    float mvErrorPercent;

    int mvSocket;
    int mvFrom;
    sockaddr_storage mvAddr;
    socklen_t mvAddrLen;

    std::string mvFromName;
    unsigned int mvBufferSize;
    unsigned int mvWindowSize;

    /** Outgoing window */
    std::deque<packet> mvWindow;
    std::deque<packet> mvOutBuf;
    
    unsigned short mvPort;
    unsigned int mvSequence;
    int mvRetries;
    bool mvInitialized;
    
    enum State {
        INIT,
        ERROR,
        DONE,
        FILL_WINDOW,
        SEND_WINDOW,
        WAIT_RR
    } mvState;
    
    int recvPacket(packet &buf);
    
    State init();
    State fillWindow();
    State sendWindow();
    State waitRR();
    
};

#endif // SERVER_H
