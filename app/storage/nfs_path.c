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

// --- Write scope (see nfs.h) -----------------------------------------------------------------

static char g_scope[NFS_PATH_MAX];
static bool g_scope_active;

nfs_err_t nfs_write_scope_enter(const char *dir) {
    char norm[NFS_PATH_MAX];
    nfs_err_t e = nfs_path_normalize(dir, norm, sizeof norm);
    if (e != NFS_OK) {
        return e;
    }
    if (strcmp(norm, "/") == 0 || g_scope_active) {
        return NFS_ERR_INVALID; // the whole volume is not a scope; scopes do not nest
    }
    memcpy(g_scope, norm, strlen(norm) + 1);
    g_scope_active = true;
    return NFS_OK;
}

void nfs_write_scope_leave(void) {
    g_scope_active = false;
    g_scope[0] = '\0';
}

bool nfs_write_scope_active(void) { return g_scope_active; }

bool nfs_write_scope_allows(const char *path) {
    if (!g_scope_active) {
        return true;
    }
    char norm[NFS_PATH_MAX];
    if (path == NULL || nfs_path_normalize(path, norm, sizeof norm) != NFS_OK) {
        return false;
    }
    size_t n = strlen(g_scope);
    // Component-wise: "/.neupod2" is not inside "/.neupod".
    return strncmp(norm, g_scope, n) == 0 && (norm[n] == '\0' || norm[n] == '/');
}
