#ifndef SDHCI_ENGINE_H
#define SDHCI_ENGINE_H

#include <Kernel.h>

namespace BPrivate {

struct SdhciTransaction {
    uint32 command;
    uint32 block;
    uint32 count;
    void* buffer;
    bool is_write;
    uint32 epoch;
};

class SdhciEngine {
public:
    SdhciEngine();
    virtual ~SdhciEngine();

    status_t Init(uintptr_t regs, int32 irq);
    status_t Submit(SdhciTransaction& transaction, uint32* epochOut);
    status_t Wait(uint32 epoch, uint32 timeout, status_t* resultOut);
    status_t Reset();
    void HandleInterrupt();

private:
    // Internal state
    uintptr_t fRegs;
    int32 fIrq;
};

} // namespace BPrivate

#endif // SDHCI_ENGINE_H
