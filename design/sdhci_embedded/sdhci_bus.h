#ifndef SDHCI_BUS_H
#define SDHCI_BUS_H

#include <Kernel.h>

namespace BPrivate {

class SdhciBus {
public:
    virtual ~SdhciBus() {}

    virtual status_t Enumerate() = 0;
    virtual status_t ReadBlocks(uint32 block, uint32 count, void* buffer) = 0;
    virtual status_t WriteBlocks(uint32 block, uint32 count, const void* buffer) = 0;
};

} // namespace BPrivate

#endif // SDHCI_BUS_H
