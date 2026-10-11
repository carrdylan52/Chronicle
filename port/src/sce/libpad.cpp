#include <libpad.h>

#include "sce_core/pad.hpp"

namespace pad = sce_core::pad;

int scePadInit(int mode) {
    pad::Init();
    return 1;
}

int scePadPortOpen(int port, int slot, void *buffer) {
    return pad::Open(port) ? 1 : 0;
}

int scePadRead(int port, int slot, unsigned char *data) {
    return pad::Read(port, data);
}

int scePadGetState(int port, int slot) {
    return pad::Ready(port) ? scePadStateStable : scePadStateDiscon;
}

int scePadInfoMode(int port, int slot, int info, int index) {
    if (!pad::Ready(port)) {
        return 0;
    }
    return info == InfoModeCurID ? pad::kTerminalId : 0;
}

int scePadSetMainMode(int port, int slot, int mode, int lock) {
    return pad::Ready(port) ? 1 : 0;
}

int scePadInfoAct(int port, int slot, int actuator, int command) {
    return pad::ActuatorInfo(port, actuator);
}

int scePadSetActAlign(int port, int slot, unsigned char *alignment) {
    return pad::Ready(port) ? 1 : 0;
}

int scePadSetActDirect(int port, int slot, unsigned char *values) {
    return pad::SetActuators(port, values) ? 1 : 0;
}
