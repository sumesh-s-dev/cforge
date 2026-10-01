#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static void skip_ws(const uint8_t *s, size_t n, size_t *i) {
    while (*i < n) {
        uint8_t c = s[*i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            break;
        }
        (*i)++;
    }
}

static int utf8_ok_seq(const uint8_t *s, size_t n, size_t i, size_t *used) {
    if (i >= n) {
        return 0;
    }
    uint8_t c = s[i];
    if (c <= 0x7F) {
        *used = 1;
        return 1;
    }
    size_t need = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        need = 2;
        cp = c & 0x1F;
        if (c < 0xC2) {
            return 0;
        }
    } else if ((c & 0xF0) == 0xE0) {
        need = 3;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 4;
        cp = c & 0x07;
        if (c > 0xF4) {
            return 0;
        }
    } else {
        return 0;
    }
    if (i + need > n) {
        return 0;
    }
    for (size_t k = 1; k < need; k++) {
        if ((s[i + k] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (s[i + k] & 0x3F);
    }
    if (need == 3 && cp < 0x800) {
        return 0;
    }
    if (need == 4 && cp < 0x10000) {
        return 0;
    }
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    *used = need;
    return 1;
}

static int hex_nibble(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int append_cp(uint8_t *dst, size_t cap, size_t *len, uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    uint8_t tmp[4];
    size_t n = 0;
    if (cp <= 0x7F) {
        tmp[0] = (uint8_t)cp;
        n = 1;
    } else if (cp <= 0x7FF) {
        tmp[0] = (uint8_t)(0xC0 | (cp >> 6));
        tmp[1] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp <= 0xFFFF) {
        tmp[0] = (uint8_t)(0xE0 | (cp >> 12));
        tmp[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        tmp[0] = (uint8_t)(0xF0 | (cp >> 18));
        tmp[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (*len + n > cap) {
        return 0;
    }
    memcpy(dst + *len, tmp, n);
    *len += n;
    return 1;
}

static int parse_string(Ctx *ctx, const uint8_t *s, size_t n, size_t *i, Slice *out) {
    if (*i >= n || s[*i] != '"') {
        return 0;
    }
    (*i)++;
    uint8_t tmp[CFORGE_MAX_NAME];
    size_t len = 0;
    while (*i < n) {
        uint8_t c = s[*i];
        if (c == '"') {
            (*i)++;
            uint8_t *dst = cforge_arena_alloc(ctx->arena, len == 0 ? 1 : len);
            if (!dst) {
                return 0;
            }
            if (len > 0) {
                memcpy(dst, tmp, len);
            }
            out->ptr = dst;
            out->len = len;
            return 1;
        }
        if (c == '\\') {
            (*i)++;
            if (*i >= n) {
                return 0;
            }
            uint8_t e = s[(*i)++];
            uint32_t cp = 0;
            if (e == '"' || e == '\\' || e == '/') {
                cp = e;
            } else if (e == 'b') {
                cp = '\b';
            } else if (e == 'f') {
                cp = '\f';
            } else if (e == 'n') {
                cp = '\n';
            } else if (e == 'r') {
                cp = '\r';
            } else if (e == 't') {
                cp = '\t';
            } else if (e == 'u') {
                if (*i + 4 > n) {
                    return 0;
                }
                int v = 0;
                for (int k = 0; k < 4; k++) {
                    int h = hex_nibble(s[*i + (size_t)k]);
                    if (h < 0) {
                        return 0;
                    }
                    v = (v << 4) | h;
                }
                *i += 4;
                cp = (uint32_t)v;
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    return 0;
                }
            } else {
                return 0;
            }
            if (!append_cp(tmp, CFORGE_MAX_NAME, &len, cp)) {
                return 0;
            }
            continue;
        }
        if (c < 0x20) {
            return 0;
        }
        size_t used = 0;
        if (!utf8_ok_seq(s, n, *i, &used)) {
            return 0;
        }
        if (len + used > CFORGE_MAX_NAME) {
            return 0;
        }
        memcpy(tmp + len, s + *i, used);
        len += used;
        *i += used;
    }
    return 0;
}

static int parse_u32(const uint8_t *s, size_t n, size_t *i, uint32_t *out) {
    if (*i >= n || s[*i] < '0' || s[*i] > '9') {
        return 0;
    }
    if (s[*i] == '0') {
        (*i)++;
        if (*i < n && s[*i] >= '0' && s[*i] <= '9') {
            return 0;
        }
        *out = 0;
        return 1;
    }
    uint64_t v = 0;
    while (*i < n && s[*i] >= '0' && s[*i] <= '9') {
        v = v * 10u + (uint64_t)(s[*i] - '0');
        if (v > UINT32_MAX) {
            return 0;
        }
        (*i)++;
    }
    *out = (uint32_t)v;
    return 1;
}

static int key_is(Slice key, const char *lit) {
    size_t L = strlen(lit);
    if (key.len != L || !key.ptr) {
        return 0;
    }
    return memcmp(key.ptr, lit, L) == 0;
}

int32_t json_parse_create(Ctx *ctx, Slice *name, uint32_t *age) {
    const uint8_t *s = ctx->body.ptr;
    size_t n = ctx->body.len;
    size_t i = 0;
    int saw_name = 0;
    int saw_age = 0;
    if (!s && n > 0) {
        return -1;
    }
    if (!s) {
        s = (const uint8_t *)"";
        n = 0;
    }
    skip_ws(s, n, &i);
    if (i >= n || s[i] != '{') {
        return -1;
    }
    i++;
    skip_ws(s, n, &i);
    if (i < n && s[i] == '}') {
        return -1;
    }
    while (i < n) {
        skip_ws(s, n, &i);
        Slice key;
        key.ptr = NULL;
        key.len = 0;
        if (!parse_string(ctx, s, n, &i, &key)) {
            return -1;
        }
        skip_ws(s, n, &i);
        if (i >= n || s[i] != ':') {
            return -1;
        }
        i++;
        skip_ws(s, n, &i);
        if (key_is(key, "name")) {
            if (saw_name || !parse_string(ctx, s, n, &i, name)) {
                return -1;
            }
            saw_name = 1;
        } else if (key_is(key, "age")) {
            if (saw_age || !parse_u32(s, n, &i, age)) {
                return -1;
            }
            saw_age = 1;
        } else {
            return -1;
        }
        skip_ws(s, n, &i);
        if (i < n && s[i] == ',') {
            i++;
            continue;
        }
        if (i < n && s[i] == '}') {
            i++;
            skip_ws(s, n, &i);
            if (i != n || !saw_name || !saw_age) {
                return -1;
            }
            return 0;
        }
        return -1;
    }
    return -1;
}
