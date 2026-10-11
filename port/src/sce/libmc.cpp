#include <libmc.h>

#include "sce_core/mc_store.hpp"

namespace mc = sce_core::mc;

static_assert(sceMcResSucceed == mc::kSucceed && sceMcResNoFormat == mc::kNoFormat &&
              sceMcResFullDevice == mc::kFullDevice && sceMcResNoEntry == mc::kNoEntry &&
              sceMcResDeniedPermit == mc::kDeniedPermit && sceMcResFailReplace == mc::kFailReplace);
static_assert(sceMcTypeNoCard == mc::kTypeNoCard && sceMcTypePS2 == mc::kTypePs2);
static_assert(sceMcIniSucceed == 0);

int sceMcInit() {
    return mc::Init();
}

int sceMcSync(int mode, int *cmd, int *result) {
    return mc::Sync(cmd, result);
}

int sceMcGetInfo(int port, int slot, int *type, int *free_size, int *formatted) {
    return mc::GetInfo(port, type, free_size, formatted);
}

int sceMcOpen(int port, int slot, char *name, int flag) {
    return mc::Open(port, name, flag);
}

int sceMcClose(int fd) {
    return mc::Close(fd);
}

int sceMcRead(int fd, void *buffer, int size) {
    return mc::Read(fd, buffer, size);
}

int sceMcWrite(int fd, void *buffer, int size) {
    return mc::Write(fd, buffer, size);
}

int sceMcFlush(int fd) {
    return mc::Flush(fd);
}

int sceMcChdir(int port, int slot, char *name, char *current) {
    return mc::Chdir(port, name, current);
}

int sceMcMkdir(int port, int slot, char *name) {
    return mc::Mkdir(port, name);
}

int sceMcDelete(int port, int slot, char *name) {
    return mc::Delete(port, name);
}

int sceMcGetDir(int port, int slot, char *name, unsigned int mode, int count, void *table) {
    return mc::GetDir(port, name, mode, count, table);
}

int sceMcFormat(int port, int slot) {
    return mc::Format(port);
}

int sceMcUnformat(int port, int slot) {
    return mc::Unformat(port);
}
