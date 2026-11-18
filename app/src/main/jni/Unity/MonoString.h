#ifndef MONOSTRING_H
#define MONOSTRING_H

#include <string>
#include <stdint.h>

#ifdef __GNUC__
#include <endian.h>
#endif

using namespace std;

class MonoString {
    void *klass;
    void *monitor;
    int length;
    char chars[1];
    char *getChars() {
        return chars;
    }

public:
    int getLength() {
        return length;
    }
	
    const char *toChars();
	
    string toString();

    void setMonoString(const char *s);

    void setMonoString(string s);
};

#include "UtfHelpers.h"

#endif //MONOSTRING_H

