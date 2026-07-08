#ifndef SDHCI_MMC_BUS_H
#define SDHCI_MMC_BUS_H

#include "sdhci_bus.h"

namespace BPrivate {

class MmcBus : public SdhciBus {
public:
    MmcBus(class SdhciEngine* engine);
    virtual ~MmcBus();

    virtual status_t Enumerate() override;
    virtual status_t ReadBlocks(uint32 block, uint32 count, void* buffer) override;
    virtual status_t WriteBlocks(uint32 block, uint32 count, const void* buffer) override;

private:
    class SdhciEngine* fEngine;
};

} // namespace BPrivate

#endif // SDHCI_MMC_BUS_H
