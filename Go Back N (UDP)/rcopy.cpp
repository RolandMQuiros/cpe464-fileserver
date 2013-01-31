#include <iostream>
#include <cstdlib>
#include "Client.h"
#include "Exception.h"
#include "cpe464.h"

#define NUM_ARGS 8
#define ARG_FROM 1
#define ARG_TO 2
#define ARG_BUFSZ 3
#define ARG_PERR 4
#define ARG_WINSZ 5
#define ARG_REMNAME 6
#define ARG_REMPORT 7

int main(int argc, char *argv[]) {
    // check arguments
    if (argc != NUM_ARGS) {
        std::cerr << "usage: " << argv[0] << "from-remote-file to-local-file "
                     "buffer-size error-percent window-size remote-machine "
                     "remote-port" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Initialize errors
    sendErr_init(atof(argv[ARG_PERR]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
    
    // Create client
    try {
        Client rcopy(argv[ARG_FROM], argv[ARG_TO], atoi(argv[ARG_BUFSZ]),
                     atof(argv[ARG_PERR]),atoi(argv[ARG_WINSZ]),
                     argv[ARG_REMNAME], argv[ARG_REMPORT]);
        rcopy.Run();
    } catch (Exception &e) {
        std::cerr << e.What() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
