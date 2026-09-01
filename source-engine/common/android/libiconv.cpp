/*
 * Minimal iconv implementation for Android API < 28
 * Handles UCS-2LE <-> UCS-4LE (UTF-32LE) <-> UTF-8 conversions
 * Required for tier1/strtools.cpp character encoding routines
 */

#ifdef ANDROID

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "iconv.h"

enum iconv_encoding {
    ENC_UNKNOWN = 0,
    ENC_UCS2LE,
    ENC_UCS4LE,
    ENC_UTF32LE,
    ENC_UTF8,
    ENC_UTF16LE,
    ENC_UTF16BE,
    ENC_UTF32BE
};

struct __iconv_t {
    iconv_encoding from;
    iconv_encoding to;
};

static iconv_encoding parse_encoding(const char* name) {
    if (!name) return ENC_UNKNOWN;
    if (strcmp(name, "UCS-2LE") == 0 || strcmp(name, "UTF-16LE") == 0 || strcmp(name, "utf16le") == 0)
        return ENC_UCS2LE;
    if (strcmp(name, "UCS-4LE") == 0 || strcmp(name, "UTF-32LE") == 0 || strcmp(name, "utf32le") == 0 || strcmp(name, "wchar_t") == 0)
        return ENC_UCS4LE;
    if (strcmp(name, "UTF-8") == 0 || strcmp(name, "utf8") == 0 || strcmp(name, "ASCII") == 0)
        return ENC_UTF8;
    if (strcmp(name, "UTF-16") == 0 || strcmp(name, "utf16") == 0)
        return ENC_UTF16LE;
    if (strcmp(name, "UTF-16BE") == 0 || strcmp(name, "utf16be") == 0)
        return ENC_UTF16BE;
    if (strcmp(name, "UTF-32") == 0 || strcmp(name, "utf32") == 0)
        return ENC_UTF32LE;
    if (strcmp(name, "UTF-32BE") == 0 || strcmp(name, "utf32be") == 0)
        return ENC_UTF32BE;
    return ENC_UNKNOWN;
}

extern "C" {

iconv_t iconv_open(const char* tocode, const char* fromcode) {
    iconv_encoding from = parse_encoding(fromcode);
    iconv_encoding to = parse_encoding(tocode);
    if (from == ENC_UNKNOWN || to == ENC_UNKNOWN) {
        errno = EINVAL;
        return (iconv_t)(-1);
    }
    __iconv_t* conv = (__iconv_t*)malloc(sizeof(__iconv_t));
    if (!conv) {
        errno = ENOMEM;
        return (iconv_t)(-1);
    }
    conv->from = from;
    conv->to = to;
    return conv;
}

static unsigned int read_codepoint(const unsigned char** src, size_t* remaining, iconv_encoding enc) {
    if (*remaining == 0) return 0;

    switch (enc) {
    case ENC_UCS2LE: {
        if (*remaining < 2) return 0;
        unsigned int cp = (*src)[0] | ((*src)[1] << 8);
        *src += 2;
        *remaining -= 2;
        return cp;
    }
    case ENC_UCS4LE:
    case ENC_UTF32LE: {
        if (*remaining < 4) return 0;
        unsigned int cp = (*src)[0] | ((*src)[1] << 8) | ((*src)[2] << 16) | ((*src)[3] << 24);
        *src += 4;
        *remaining -= 4;
        return cp;
    }
    case ENC_UTF16LE: {
        if (*remaining < 2) return 0;
        unsigned int cp = (*src)[0] | ((*src)[1] << 8);
        *src += 2;
        *remaining -= 2;
        if (cp >= 0xD800 && cp <= 0xDBFF && *remaining >= 2) {
            unsigned int lo = (*src)[0] | ((*src)[1] << 8);
            *src += 2;
            *remaining -= 2;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
        }
        return cp;
    }
    case ENC_UTF8: {
        unsigned int cp = 0;
        int len = 0;
        if ((*src)[0] < 0x80) { cp = (*src)[0]; len = 1; }
        else if ((*src)[0] < 0xE0) { cp = (*src)[0] & 0x1F; len = 2; }
        else if ((*src)[0] < 0xF0) { cp = (*src)[0] & 0x0F; len = 3; }
        else { cp = (*src)[0] & 0x07; len = 4; }
        if ((size_t)len > *remaining) return 0;
        (*src)++;
        (*remaining)--;
        for (int i = 1; i < len; i++) {
            cp = (cp << 6) | ((*src)[0] & 0x3F);
            (*src)++;
            (*remaining)--;
        }
        return cp;
    }
    default:
        return 0;
    }
}

static int write_codepoint(unsigned char** dst, size_t* remaining, unsigned int cp, iconv_encoding enc) {
    switch (enc) {
    case ENC_UCS2LE: {
        if (cp > 0xFFFF) return -1;
        if (*remaining < 2) return -1;
        (*dst)[0] = cp & 0xFF;
        (*dst)[1] = (cp >> 8) & 0xFF;
        *dst += 2;
        *remaining -= 2;
        return 0;
    }
    case ENC_UCS4LE:
    case ENC_UTF32LE: {
        if (*remaining < 4) return -1;
        (*dst)[0] = cp & 0xFF;
        (*dst)[1] = (cp >> 8) & 0xFF;
        (*dst)[2] = (cp >> 16) & 0xFF;
        (*dst)[3] = (cp >> 24) & 0xFF;
        *dst += 4;
        *remaining -= 4;
        return 0;
    }
    case ENC_UTF16LE: {
        if (cp < 0x10000) {
            if (*remaining < 2) return -1;
            (*dst)[0] = cp & 0xFF;
            (*dst)[1] = (cp >> 8) & 0xFF;
            *dst += 2;
            *remaining -= 2;
        } else {
            if (*remaining < 4) return -1;
            cp -= 0x10000;
            unsigned int hi = 0xD800 + (cp >> 10);
            unsigned int lo = 0xDC00 + (cp & 0x3FF);
            (*dst)[0] = hi & 0xFF;
            (*dst)[1] = (hi >> 8) & 0xFF;
            (*dst)[2] = lo & 0xFF;
            (*dst)[3] = (lo >> 8) & 0xFF;
            *dst += 4;
            *remaining -= 4;
        }
        return 0;
    }
    case ENC_UTF8: {
        int len;
        if (cp < 0x80) len = 1;
        else if (cp < 0x800) len = 2;
        else if (cp < 0x10000) len = 3;
        else len = 4;
        if ((size_t)len > *remaining) return -1;
        switch (len) {
        case 1: (*dst)[0] = cp; break;
        case 2: (*dst)[0] = 0xC0 | (cp >> 6); (*dst)[1] = 0x80 | (cp & 0x3F); break;
        case 3: (*dst)[0] = 0xE0 | (cp >> 12); (*dst)[1] = 0x80 | ((cp >> 6) & 0x3F); (*dst)[2] = 0x80 | (cp & 0x3F); break;
        case 4: (*dst)[0] = 0xF0 | (cp >> 18); (*dst)[1] = 0x80 | ((cp >> 12) & 0x3F); (*dst)[2] = 0x80 | ((cp >> 6) & 0x3F); (*dst)[3] = 0x80 | (cp & 0x3F); break;
        }
        *dst += len;
        *remaining -= len;
        return 0;
    }
    default:
        return -1;
    }
}

size_t iconv(iconv_t cd, char** inbuf, size_t* inbytesleft, char** outbuf, size_t* outbytesleft) {
    if (!cd || cd == (iconv_t)(-1)) {
        errno = EBADF;
        return (size_t)(-1);
    }
    __iconv_t* conv = (__iconv_t*)cd;

    size_t total_converted = 0;

    while (*inbytesleft > 0 && *outbytesleft > 0) {
        const unsigned char* src_save = (const unsigned char*)*inbuf;
        unsigned int cp = read_codepoint((const unsigned char**)inbuf, inbytesleft, conv->from);
        if (cp == 0 && *inbytesleft == 0) break;

        if (write_codepoint((unsigned char**)outbuf, outbytesleft, cp, conv->to) < 0) {
            errno = E2BIG;
            return (size_t)(-1);
        }
        total_converted++;
    }

    return total_converted;
}

int iconv_close(iconv_t cd) {
    if (!cd || cd == (iconv_t)(-1)) {
        errno = EBADF;
        return -1;
    }
    free(cd);
    return 0;
}

} /* extern "C" */

#endif /* ANDROID */
