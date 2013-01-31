#include "GetSocket.h"

int GetSocket(sockaddr_in &local, socklen_t &len) {
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