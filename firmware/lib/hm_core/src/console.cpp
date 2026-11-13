#include "console.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace hm {

namespace {

const char* skipSpace(const char* s){
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Copies the next word (lower-cased) into out; returns the rest
const char* word(const char* s, char* out, size_t n){
    s = skipSpace(s);
    size_t i = 0;
    while (*s && *s != ' ' && *s != '\t'){
        if (i < n - 1){
            out[i++] = static_cast<char>(tolower(static_cast<unsigned char>(*s)));
        }
        s++;
    }
    out[i] = 0;
    return s;
}

// The rest of the line, trimmed, case kept
void rest(const char* s, char* out, size_t n){
    s = skipSpace(s);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    if (len > n - 1) len = n - 1;
    memcpy(out, s, len);
    out[len] = 0;
}

void usage(Command& c, const char* text){
    c.kind = Cmd::BadArgs;
    strncpy(c.arg, text, sizeof(c.arg) - 1);
    c.arg[sizeof(c.arg) - 1] = 0;
}

bool parseInt(const char* s, int& out){
    if (!*s) return false;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (*end) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseFloat(const char* s, float& out){
    if (!*s) return false;
    char* end = nullptr;
    float v = strtof(s, &end);
    if (*end) return false;
    out = v;
    return true;
}

}  // namespace

Command parseCommand(const char* line){
    Command c;
    char w[16];
    const char* r = word(line, w, sizeof(w));
    if (!w[0]){
        c.kind = Cmd::None;
        return c;
    }
    if (strcmp(w, "help") == 0 || strcmp(w, "?") == 0){ c.kind = Cmd::Help; return c; }
    if (strcmp(w, "status") == 0){ c.kind = Cmd::Status; return c; }
    if (strcmp(w, "version") == 0){ c.kind = Cmd::Version; return c; }
    if (strcmp(w, "reboot") == 0){ c.kind = Cmd::Reboot; return c; }
    if (strcmp(w, "setup") == 0){ c.kind = Cmd::Setup; return c; }
    if (strcmp(w, "beep") == 0){ c.kind = Cmd::Beep; return c; }

    if (strcmp(w, "sound") == 0){
        char a[16];
        word(r, a, sizeof(a));
        if (strcmp(a, "on") == 0) c.value = 1;
        else if (strcmp(a, "off") == 0) c.value = 0;
        else { usage(c, "sound on|off"); return c; }
        c.kind = Cmd::Sound;
        return c;
    }
    if (strcmp(w, "volume") == 0){
        char a[16];
        word(r, a, sizeof(a));
        if (!parseInt(a, c.value) || c.value < 0 || c.value > 255){
            usage(c, "volume <0-255>");
            return c;
        }
        c.kind = Cmd::Volume;
        return c;
    }
    if (strcmp(w, "buzzer") == 0){
        char a[16];
        word(r, a, sizeof(a));
        if (strcmp(a, "off") == 0){
            c.value = -1;
        } else if (!parseInt(a, c.value) || c.value < 0 || c.value > 48){
            usage(c, "buzzer <gpio>|off");
            return c;
        }
        c.kind = Cmd::Buzzer;
        return c;
    }

    if (strcmp(w, "printer") == 0){
        rest(r, c.arg, sizeof(c.arg));
        if (!c.arg[0] || strchr(c.arg, ' ')){
            usage(c, "printer <ip|host> | printer auto");
            return c;
        }
        char lower[8];
        word(c.arg, lower, sizeof(lower));
        if (strcmp(lower, "auto") == 0){
            strcpy(c.arg, "auto");
        }
        c.kind = Cmd::Printer;
        return c;
    }
    if (strcmp(w, "tz") == 0){
        rest(r, c.arg, sizeof(c.arg));
        if (!c.arg[0]){
            usage(c, "tz <POSIX TZ>, e.g. tz GMT0BST,M3.5.0/1,M10.5.0");
            return c;
        }
        c.kind = Cmd::Tz;
        return c;
    }
    if (strcmp(w, "bright") == 0){
        char a[16];
        word(r, a, sizeof(a));
        if (!parseInt(a, c.value) || c.value < 0 || c.value > 255){
            usage(c, "bright <0-255>");
            return c;
        }
        c.kind = Cmd::Bright;
        return c;
    }
    if (strcmp(w, "wifi") == 0){
        char a[16];
        word(r, a, sizeof(a));
        if (strcmp(a, "reset") != 0){
            usage(c, "wifi reset");
            return c;
        }
        c.kind = Cmd::WifiReset;
        return c;
    }
    if (strcmp(w, "calibrate") == 0){
        const char* kUsage = "calibrate rot=<0-3> sign=<1|-1> offset=<deg> | reset";
        char a[32];
        bool any = false;
        for (;;){
            r = word(r, a, sizeof(a));
            if (!a[0]) break;
            any = true;
            if (strcmp(a, "reset") == 0){
                c.reset = true;
                continue;
            }
            char* eq = strchr(a, '=');
            if (!eq){
                usage(c, kUsage);
                return c;
            }
            *eq = 0;
            const char* val = eq + 1;
            if (strcmp(a, "rot") == 0){
                if (!parseInt(val, c.rot) || c.rot < 0 || c.rot > 3){ usage(c, kUsage); return c; }
                c.hasRot = true;
            } else if (strcmp(a, "sign") == 0){
                if (!parseInt(val, c.sign) || (c.sign != 1 && c.sign != -1)){ usage(c, kUsage); return c; }
                c.hasSign = true;
            } else if (strcmp(a, "offset") == 0){
                if (!parseFloat(val, c.offsetDeg) || c.offsetDeg < -360.0f || c.offsetDeg > 360.0f){ usage(c, kUsage); return c; }
                c.hasOffset = true;
            } else {
                usage(c, kUsage);
                return c;
            }
        }
        if (!any || (c.reset && (c.hasRot || c.hasSign || c.hasOffset))){
            usage(c, kUsage);
            return c;
        }
        c.kind = Cmd::Calibrate;
        return c;
    }
    c.kind = Cmd::Unknown;
    rest(line, c.arg, sizeof(c.arg));
    return c;
}

bool LineReader::feed(char ch){
    if (ready_){
        len_ = 0;
        buf_[0] = 0;
        ready_ = false;
    }
    if (ch == '\r' || ch == '\n'){
        if (len_ == 0){
            return false;   // the LF of a CRLF, or a blank line
        }
        buf_[len_] = 0;
        ready_ = true;
        return true;
    }
    if (len_ < sizeof(buf_) - 1){
        buf_[len_++] = ch;
    }
    return false;
}

}  // namespace hm
