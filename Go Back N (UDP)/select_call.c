#include <stdio.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _POSIX_C_SOURCE
    #include <sys/select.h>
#endif

#include "select_call.h"

int select_call(int socket, int seconds, int useconds) {
    struct timeval timeout = { seconds, useconds };
    fd_set readfds;
    int ready;
    
    /* Add socket to a set */
    FD_ZERO(&readfds);
    FD_SET(socket, &readfds);
    
    /* Monitor socket until it's readable, or we time out*/
    if ((ready = select(socket + 1, &readfds, NULL, NULL, &timeout)) == -1) {
        perror("select_call");
        ready = 0;
    }
    
    return ready;
}
