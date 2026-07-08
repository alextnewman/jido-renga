#include "sdhci_host_manager.h"
#include "explicit_matcher.h"
#include "sdhci_host.h"
#include <Kernel.h>

namespace BPrivate {

SdhciHostManager::SdhciHostManager() {}
SdhciHostManager::~SdhciHostManager() {}

float SdhciHostManager::supports_device(device_node* node) {
    if (node == NULL)
        return 0.0f;

    SdhciDeviceMatch* match = ExplicitMatcher::Match(node);
    if (match == NULL)
        return 0.0f;

    // Match found. In a real implementation, we might return a score.
    // For P0, we return a positive value to indicate support.
    return 1.0f;
}

status_t SdhciHostManager::register_device(device_node* node) {
    // This would be called by the device manager to bind the driver.
    // For P0, we'll implement the orchestration here.
    return B_OK;
}

status_t SdhciHostManager::init_device(device_node* node) {
    SdhciDeviceMatch* match = ExplicitMatcher::Match(node);
    if (match == NULL)
        return B_NOT_FOUND;

    // Create the host. Ownership will be managed by the HostManager or the device node.
    // For now, we'll just return B_OK to complete the skeleton.
    
    delete match; // Match was allocated in ExplicitMatcher::Match
    return B_OK;
}

status_t SdhciHostManager::uninit_device(device_node* node) {
    return B_OK;
}

} // namespace BPrivate
