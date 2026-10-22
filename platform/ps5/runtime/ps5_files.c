/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Original bindings for libc names unavailable to a native title on this SDK. */
#include <ps5platform/libc.h>
#include <errno.h>
#include <sys/types.h>
int oq_mkstemp(char *name) {return ps5_mkstemps(name,0);}
ssize_t oq_readlink(const char *path,char *buffer,size_t bytes) {
    /* This title has no accessible readlink provider. Mesa may probe process
     * paths; report an ordinary unsupported operation instead of a null import. */
    (void)path;(void)buffer;(void)bytes;errno=ENOSYS;return -1;
}
