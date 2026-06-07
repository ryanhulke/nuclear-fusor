#include "Ble/BleScan.h"

std::mutex& bleScanMutex() {
    static std::mutex mutex;
    return mutex;
}
