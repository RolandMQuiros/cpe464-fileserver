#include <sstream>
#include "Exception.h"

Exception::Exception(unsigned long line, const std::string &who,
                             const std::string &what) {
    std::stringstream ss;
    ss << __FILE__ << " (" << line << ") " << std::endl
       << "    " << who << ": " << what;
    mvWhat = ss.str();
}

std::string Exception::What() const {
    return mvWhat;
}
