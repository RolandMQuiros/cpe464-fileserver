#include <iostream>
#include <cstdlib>

#include "Server.h"
#include "Exception.h"
#include "cpe464.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " error-percent" << std::endl;
        return EXIT_FAILURE;
    }
    
    // Initialize errors
    sendErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
    
    try {
        Server server(atof(argv[1]));
        if (server.Run()) {
            return EXIT_FAILURE;
        }
    } catch (Exception &e) {
        std::cerr << e.What() << std::endl;
    }

    return EXIT_SUCCESS;
}
