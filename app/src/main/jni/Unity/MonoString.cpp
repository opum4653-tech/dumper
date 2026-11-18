#include <cstring>
#include <string>
#include <cstdlib>
#include "MonoString.h"

using namespace std;

void MonoString::setMonoString(const char *s) {
    string str(s);
    length = strlen(s);
    u16string basicString = utf8_to_utf16le(str);
    const char16_t *cStr = basicString.c_str();
    memcpy(getChars(), cStr, getLength() * 2);
}

void MonoString::setMonoString(string s) {
    length = s.length();
    u16string basicString = utf8_to_utf16le(s);
    const char16_t *str = basicString.c_str();
    memcpy(getChars(), str, getLength() * 2);
}

const char *MonoString::toChars() {
    u16string ss((char16_t *) getChars(), 0, getLength());
    static thread_local string str;
    str = utf16le_to_utf8(ss);
    return str.c_str();
}

string MonoString::toString() {
    u16string ss((char16_t *) getChars(), 0, getLength());
    string str = utf16le_to_utf8(ss);
    return str;
}
