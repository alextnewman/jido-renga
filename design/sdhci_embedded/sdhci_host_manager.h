#ifndef SDHCI_HOST_MANAGER_H
#define SDHCI_HOST_MANAGER_H

#include <Kernel.h>

namespace BPrivate {

class SdhciHostManager {
public:
    SdhciHostManager();
    ~SdhciHostManager();

    // Device Manager interface
    float supports_device(device_node* node);
    status_t register_device(device_node* node);
    status_t init_device(device_node* node);
    status_t uninit_device(device_node* node);

private:
    // Implementation details
};

} // namespace BPrivate

#endif // SDHCI_HOST_MANAGER_H
