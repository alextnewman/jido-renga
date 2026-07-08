#include "explicit_matcher.h"
#include <Kernel.h>

namespace BPrivate {

struct SdhciStaticEntry {
    const char* hid;
    uint32 uid;
    SdhciBusType bus_type;
    bool non_removable;
};

static const SdhciStaticEntry kStaticTable[] = {
    {"80860F16", 3, SdhciBusType::SD, false},
    {"80860F14", 1, SdhciBusType::MMC, true},
};

SdhciDeviceMatch* ExplicitMatcher::Match(device_node* node) {
    if (node == NULL)
        return NULL;

    const char* hid = NULL;
    uint32 uid = 0;

    // Attempt to get HID and UID from the node's attributes.
    // In a real ACPI implementation, these would be the _HID and UID attributes.
    if (get_attr_string(node, "HID", &hid, false) != B_OK)
        return NULL;

    if (get_attr_uint32(node, "UID", &uid, false) != B_OK)
        return NULL;

    for (size_t i = 0; i < sizeof(kStaticTable) / sizeof(kStaticTable[0]); i++) {
        if (strcmp(kStaticTable[i].hid, hid) == 0 && kStaticTable[i].uid == uid) {
            SdhciDeviceMatch* match = new(std::nothrow) SdhciDeviceMatch();
            if (match == NULL)
                return NULL;

            match->bus_type = kStaticTable[i].bus_type;
            match->non_removable = kStaticTable[i].non_removable;
            return match;
        }
    }

    return NULL;
}

} // namespace BPrivate
