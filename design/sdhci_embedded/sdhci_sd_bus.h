#ifndef SDHCI_SD_BUS_H
#define SDHCI_SD_BUS_H

#include "sdhci_bus.h"

namespace BPrivate {

class SdBus : public SdhciBus {
public:
    SdBus(class SdhciEngine* engine);
    virtual ~SdBus();

    virtual status_t Enumerate() override;
    virtual status_t ReadBlocks(uint32 block, uint32 count, void* buffer) override;
    virtual status_t WriteBlocks(uint32 block, uint32 count, const void* buffer) override;

private:
    class SdhciEngine* fEngine;
};

} // namespace BPrivate

#endif // SDHCI_SD_BUS_H
