#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <string>

class Exception {
public:
    Exception(unsigned long line, const std::string &who,
              const std::string &what);
    std::string What() const;
private:
    std::string mvWhat;
};

#endif // EXCEPTION_H
