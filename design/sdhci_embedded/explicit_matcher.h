#ifndef EXPLICIT_MATCHER_H
#define EXPLICIT_MATCHER_H

#include <Kernel.h>

namespace BPrivate {

enum class SdhciBusType {
    SD,
    MMC
};

struct SdhciDeviceMatch {
    SdhciBusType bus_type;
    bool non_removable;
};

class ExplicitMatcher {
public:
    /**
     * Matches the given ACPI device node against the static table.
     * Returns a pointer to a match structure if successful, otherwise NULL.
     */
    static SdhciDeviceMatch* Match(device_node* node);
};

} // namespace BPrivate

#endif // EXPLICIT_MATCHER_H
