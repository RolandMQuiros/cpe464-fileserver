#ifndef CLIENT_H
#define CLIENT_H

#include <set>
#include <string>
#include <stdexcept>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "packet.h"

class Client {
public:
    enum State {
        INIT,
        
    };
    
    enum NetState {
        TRANSMIT,
        RECEIVE
    };

    Client::Client(const std::string& serverName, const std::string &port,
                   const addrinfo &hints);
    ~Client();
    bool IsValid() const;
    void OpenFile(const std::string &fileName);
    packet GetNext();
    void Update();
    
    struct Exception : public std::runtime_error {
        Exception(const std::string &where, const std::string &what);
    };
private:
    /** Socket file descriptor */
    int mvSocket;
    /** Local file descriptor */
    int mvLocalFile;
    /** Label string for server */
    std::string mvServLabel;
    
    /** Client state */
    State mvState;

    /** Network state */
    NetState mvNetState;
    
    /** Sequence counter */
    unsigned int mvSequence;

    /** Retransmission counter */
    unsigned int mvRetries;
    
    /** Output packet */
    packet mvOutPacket;
    
    /** Input packet */
    packet mvInPacket;
    
    void SendPacket();
};

#endif
