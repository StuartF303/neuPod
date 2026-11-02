// SPDX-License-Identifier: MPL-2.0
// Backend-independent parts of the portable file API: path normalisation and error names.
#include <string.h>

#include "nfs.h"

const char *nfs_err_str(nfs_err_t e) {
    switch (e) {
    case NFS_OK: return "OK";
    case NFS_ERR_NOT_FOUND: return "NOT_FOUND";
    case NFS_ERR_EXISTS: return "EXISTS";
    case NFS_ERR_NOT_DIR: return "NOT_DIR";
    case NFS_ERR_IS_DIR: return "IS_DIR";
    case NFS_ERR_NOT_EMPTY: return "NOT_EMPTY";
    case NFS_ERR_NO_SPACE: return "NO_SPACE";
    case NFS_ERR_IO: return "IO";
    case NFS_ERR_INVALID: return "INVALID";
    case NFS_ERR_BAD_PATH: return "BAD_PATH";
    case NFS_ERR_TOO_MANY: return "TOO_MANY";
    case NFS_ERR_NOT_MOUNTED: return "NOT_MOUNTED";
    case NFS_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case NFS_ERR_NO_MEMORY: return "NO_MEMORY";
    }
    return "?";
}

nfs_err_t nfs_path_normalize(const char *in, char *out, size_t cap) {
    if (in == NULL || out == NULL || cap < 2 || in[0] == '\0') {
        return NFS_ERR_BAD_PATH;
    }
    size_t len = 1; // out so far: "/" at out[0], components appended as "name/" ... trimmed below
    out[0] = '/';
    const char *p = in;
    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        const char *start = p;
        while (*p != '/' && *p != '\0') {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7F || c == '\\') {
                return NFS_ERR_BAD_PATH;
            }
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n == 0 || (n == 1 && start[0] == '.')) {
            continue;
        }
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (len == 1) {
                return NFS_ERR_BAD_PATH; // would climb above the root
            }
            len--; // drop the trailing '/'
            while (len > 1 && out[len - 1] != '/') {
                len--;
            }
            continue;
        }
        if (n > NFS_NAME_MAX || len + n + 1 >= cap) {
            return NFS_ERR_BAD_PATH;
        }
        memcpy(&out[len], start, n);
        len += n;
        out[len++] = '/';
    }
    if (len > 1) {
        len--; // strip the trailing '/'
    }
    out[len] = '\0';
    return NFS_OK;
}
