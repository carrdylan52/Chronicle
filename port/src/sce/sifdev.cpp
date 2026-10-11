#include <sifdev.h>

#include "sce_core/host0.hpp"

// edit.cpp's host0:debug.txt dump is the one caller left: mwBload's overlay reads and InitCDFile's
// cdrom0: index read are replaced.

namespace host0 = sce_core::host0;

static_assert(SCE_RDONLY == host0::kReadOnly && SCE_WRONLY == host0::kWriteOnly && SCE_CREAT == host0::kCreate &&
              SCE_TRUNC == host0::kTruncate);

void sceFsReset() {}

int sceOpen(const char *name, int flags) {
    return host0::Open(name, flags);
}

int sceClose(int fd) {
    return host0::Close(fd);
}

int sceLseek(int fd, int offset, int whence) {
    return host0::Lseek(fd, offset, whence);
}

int sceRead(int fd, void *buffer, int size) {
    return host0::Read(fd, buffer, size);
}

int sceWrite(int fd, void *buffer, int size) {
    return host0::Write(fd, buffer, size);
}
