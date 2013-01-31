#ifndef CLIENT_H
#define CLIENT_H

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <deque>
#include <string>
#include <stdexcept>

extern "C" {
    #include "packet.h"
}

class Client {
    public:
        Client(const std::string &from, const std::string &to,
               unsigned int bufferSize, float errorPercent,
               unsigned int windowSize, const std::string &remoteMachine,
               const std::string &remotePort);
        ~Client();
    
        int GetSocket(sockaddr_in &remote);
        
        int Run();

    private:
        std::string mvFromName;
        std::string mvToName;
        unsigned int mvBufferSize;
        float mvErrorPercent;
        unsigned int mvWindowSize;
        std::string mvRemoteMachine;
        unsigned short mvRemotePort;

        int mvSocket;
        int mvOldSocket;
        int mvTo;
        sockaddr_storage mvAddr;
        socklen_t mvAddrLen;

        int mvRetries;
        unsigned int mvSequence;

        enum State {
            INIT,
            ERROR,
            DONE,            RECV_PACKETS
        } mvState;
        
        int recvPacket(packet &buf);
        int writeTo(packet &in);
        
        State init();
        State recvPackets();
};

#endif // CLIENT_H
