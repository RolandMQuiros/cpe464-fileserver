#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "client.h"
#include "cpe464.h"

Client::Client(const std::string& serverName, const std::string &port,
               const addrinfo &hints) :
mvSocket(-1),
mvLocalFile(-1),
mvSequence(0),
mvRetries(0)
{
    int tmp;
    bool found = false;
    addrinfo *server;
    addrinfo *conn;
    char servstr[INET_ADDRSTRLEN];
    
    // Get server address info
    if ((tmp = getaddrinfo(serverName.c_str(), port.c_str(), &hints,
         &server)) != 0)
    {
        throw std::runtime_error("Client: getaddrinfo",
                                 gai_strerror(tmp));
    }
    
    for (conn = server; conn != NULL && !found; conn = conn->ai_next) {
        // Create socket
        if ((mvSocket = socket(conn->ai_famly, conn->ai_socktype,
                               conn->ai_protocol)) != -1 &&
            connect(mvSocket, conn->ai_addr, conn->ai_addrlen) != -1)
        {
            found = true;
        }
    }
    
    if (conn == NULL || !found) {
        throw Exception("Client", "could not create a valid socket");
    }
    
    inet_ntop(conn->ai_family, conn->ai_addr, servstr, INET_ADDRSTRLEN);
    mvServLabel = servstr;
    
    // Release uneeded server info
    freeaddrinfo(server);
}

bool Client::IsValid() const {
    return !(mvSocket == -1 && mvLocalFile == -1);
}

void Client::OpenFile(const std::string &fileName) {
    if ((mvLocalFile = creat(filename.c_str(), filename.length(),
        S_IRWXU)) == -1)
    {
        throw Exception("Client: OpenFile: creat", strerror(errno));
    }
}

void Client::Update() {
    switch (mvState) {
        case TRANSMIT:
            if (sendErr(mvSocket, &
            break;
        case 
    }
}

Client::Exception::
Exception(const std::string &where, const std::string &what) :
std::runtime_error(where + ": " + what + '\n') {
    
}