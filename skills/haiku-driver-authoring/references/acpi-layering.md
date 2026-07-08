# ACPI layering for driver authors

This is the part that "gets super gross and weird." Read it before writing any
ACPI-touched driver. Grounded in `headers/os/drivers/ACPI.h`,
`src/add-ons/kernel/bus_managers/acpi/`,
`src/add-ons/kernel/bus_managers/i2c/I2CBus.cpp`, and
`src/add-ons/kernel/busses/i2c/pch/pch_i2c_acpi.cpp`. The living reference
implementation is `overlay/drivers/input/i2c_atmel_mxt/`.

## What the ACPI bus manager does

The ACPI bus manager (`B_ACPI_MODULE_NAME == "bus_managers/acpi/v1"`) matches
the root node, brings up ACPICA, walks the ACPI namespace from `"\"`, and
`register_node()`s **one node per ACPI device**. Each such node is tagged
`B_DEVICE_BUS == "acpi"` plus these string/int attributes (all in `ACPI.h`):

| Attribute constant         | Key            | Meaning                       |
|----------------------------|----------------|-------------------------------|
| `ACPI_DEVICE_HID_ITEM`     | `"acpi/hid"`   | Hardware ID (e.g. `PNP0C0A`)  |
| `ACPI_DEVICE_CID_ITEM`     | `"acpi/cid"`   | Compatible ID (fallback)      |
| `ACPI_DEVICE_UID_ITEM`     | `"acpi/uid"`   | Unique ID among identical HIDs|
| `ACPI_DEVICE_PATH_ITEM`    | `"acpi/path"`  | Namespace path                |
| `ACPI_DEVICE_TYPE_ITEM`    | `"acpi/type"`  | ACPI object type              |
| `ACPI_DEVICE_HANDLE_ITEM`  | `"acpi/handle"`| ACPICA handle (as uint64)     |
| `ACPI_DEVICE_ADDR_ITEM`    | `"acpi/addr"`  | `_ADR` address               |

## Two interfaces: global vs per-device

`ACPI.h` exposes two module structs. Both can evaluate methods and walk
resources, but they take different first arguments:

- **`acpi_module_info`** (`B_ACPI_MODULE_NAME`) — the global bus manager. Its
  methods take an `acpi_handle`. You obtain a handle from
  `ACPI_DEVICE_HANDLE_ITEM`. Key methods: `evaluate_method(handle, method, args,
  returnValue)`, `walk_resources(handle, method, callback, context)`,
  `walk_namespace(...)`, `get_handle(...)`, `install_notify_handler(...)`.
- **`acpi_device_module_info`** — the per-device interface (its `.info` is a
  `driver_module_info`). Same operations keyed on an `acpi_device` cookie:
  `evaluate_method`, `walk_resources(device, method, callback, context)`,
  `walk_namespace`, `install_notify_handler`, `get_object`.

`evaluate_method` returns data in an `acpi_data { acpi_size length; void*
pointer; }`. Method names are 4-char ACPI names passed as `char*` (e.g.
`"_STA"`, `"_CRS"`, `"_DSM"`).

## The three layering cases

The whole difficulty is that "an ACPI device" can mean three very different
things to a driver author:

```
  Case A: leaf ACPI device            Case B: PCI dev + ACPI companion
  ┌────────────┐                       ┌────────────┐   ┌────────────┐
  │  acpi bus  │                       │  pci bus   │   │  acpi bus  │
  └─────┬──────┘                       └─────┬──────┘   └─────┬──────┘
        │ bus="acpi", HID=PNP0C0A            │ vendor/dev     │ (companion)
  ┌─────▼──────┐                       ┌─────▼──────┐         │
  │ your driver│  <-- matches HID      │ your driver│──get_driver/attr─▶ quirks
  └────────────┘                       └────────────┘

  Case C: an ACPI device that is itself a bus (I2C/SPI/GPIO/UART)
  ┌────────────┐
  │  acpi bus  │  registers the controller node (bus="acpi", HID=<ctrl>)
  └─────┬──────┘
  ┌─────▼─────────────┐
  │ i2c controller drv│  brings up the silicon, registers an "i2c" bus node
  └─────┬─────────────┘
  ┌─────▼──────┐   the i2c bus manager enumerates slaves AND copies the
  │  i2c bus   │   child's ACPI hid/cid/handle onto each slave node
  └─────┬──────┘
  ┌─────▼──────┐   bus="i2c"  +  acpi/hid=ATML0000  +  acpi/handle=...
  │ your driver│  <-- matches on the I2C bus but still needs ACPI for _CRS
  └────────────┘
```

### Case A — leaf ACPI device

Match `B_DEVICE_BUS == "acpi"` and an exact HID. This is `acpi_battery`
(HID `PNP0C0A`), `acpi_button`, `acpi_lid`, `acpi_thermal`. Skeleton:

```cpp
static float
my_supports_device(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || strcmp(bus, "acpi") != 0)
		return 0.0f;

	const char* hid;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &hid,
			false) == B_OK && strcmp(hid, "PNP0C0A") == 0)
		return 0.6f;
	return 0.0f;
}
```

### Case B — PCI device with an ACPI companion

You match the PCI node normally (vendor/device). When you need ACPI-provided
quirks, reach the ACPI side via `get_driver()`/attributes on the companion.
`src/add-ons/kernel/busses/mmc/sdhci_acpi.cpp` is the exemplar.

### Case C — the I2C-under-ACPI trick (the one that bites)

An I2C/SPI/GPIO slave described in ACPI does **not** appear under `bus ==
"acpi"`. The I2C bus manager (`src/add-ons/kernel/bus_managers/i2c/I2CBus.cpp`)
copies the slave's ACPI `hid`/`cid` and, crucially, `ACPI_DEVICE_HANDLE_ITEM`
onto the **I2C** child node. So your driver:

1. matches `B_DEVICE_BUS == "i2c"`,
2. confirms an `ACPI_DEVICE_HANDLE_ITEM` is present (as a `uint64`),
3. matches the ACPI HID (then CID as fallback).

This is exactly what our maXTouch driver does
(`overlay/drivers/input/i2c_atmel_mxt/Driver.cpp`):

```cpp
static float
i2c_atmel_mxt_support(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || strcmp(bus, "i2c") != 0)
		return 0.0f;

	// Must carry an ACPI handle we can walk _CRS on later.
	uint64 handle;
	if (sDeviceManager->get_attr_uint64(parent, ACPI_DEVICE_HANDLE_ITEM,
			&handle, false) != B_OK)
		return 0.0f;

	const char* name;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &name,
			false) == B_OK && strcmp(name, "ATML0000") == 0)
		return 0.6f;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_CID_ITEM, &name,
			false) == B_OK && strcmp(name, "ATML0000") == 0)
		return 0.6f;
	return 0.0f;
}
```

The ACPI→I2C-controller side (the thing that made the `"i2c"` bus exist in the
first place) is `src/add-ons/kernel/busses/i2c/pch/pch_i2c_acpi.cpp` — read it
if you are writing a controller rather than a slave.

## Extracting resources: walking `_CRS` for the IRQ

The slave's interrupt is not on the I2C node; it is in the device's ACPI `_CRS`.
Grab the `acpi_module_info` handle from `ACPI_DEVICE_HANDLE_ITEM` and walk it.
`walk_resources` invokes your callback once per resource; the callback signature
is `acpi_status (*)(acpi_resource* resource, void* context)` and returns `AE_OK`
to continue. Interrupts arrive as `ACPI_RESOURCE_TYPE_IRQ` (legacy) or
`ACPI_RESOURCE_TYPE_EXTENDED_IRQ` (APIC). From our real code
(`overlay/.../i2c_atmel_mxt/MxtDevice.cpp`):

```cpp
struct mxt_crs_context {
	uint8	irq;
	uint32	irqFlags;
};

static acpi_status
_mxt_crs_callback(acpi_resource* res, void* context)
{
	mxt_crs_context* crs = static_cast<mxt_crs_context*>(context);

	if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		crs->irq = res->Data.ExtendedIrq.Interrupts[0];
		crs->irqFlags = (res->Data.ExtendedIrq.Triggering == 1)
			? B_EDGE_TRIGGERED : B_LEVEL_TRIGGERED;
		crs->irqFlags |= (res->Data.ExtendedIrq.Polarity == 0)
			? B_HIGH_ACTIVE_POLARITY : B_LOW_ACTIVE_POLARITY;
	} else if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupts[0];
		crs->irqFlags = (res->Data.Irq.Triggering == 1)
			? B_EDGE_TRIGGERED : B_LEVEL_TRIGGERED;
		crs->irqFlags |= (res->Data.Irq.Polarity == 0)
			? B_HIGH_ACTIVE_POLARITY : B_LOW_ACTIVE_POLARITY;
	}
	return AE_OK;
}

// ...in Initialize():
mxt_crs_context crs = { 0, 0 };
fACPI->walk_resources(fACPIHandle, (char*)"_CRS", _mxt_crs_callback, &crs);
if (crs.irq > 0) {
	fIRQ = crs.irq;
	fIRQFlags = crs.irqFlags;
}
```

Then install the handler (`headers/os/drivers/KernelExport.h`):

```cpp
install_io_interrupt_handler(fIRQ, my_interrupt_handler, this, 0);
```

Memory-mapped resources arrive as `ACPI_RESOURCE_TYPE_FIXED_MEMORY32` (and
address-space variants) in the same callback — switch on `res->Type` and read
`res->Data.FixedMemory32.Address` / `.AddressLength`, then
`map_physical_memory()` it.

## Gotchas (all of these have burned someone)

- **HID compare is exact and case-sensitive `strcmp`.** `ATML0000` ≠
  `atml0000`. Verify the exact string with `dmesg`/`listdev` on the target.
- **Match CID as a fallback after HID.** Many devices advertise a generic
  compatible ID (e.g. HID i2c-hid touchpads under `PNP0C50`) via `_CID`.
- **Drivers assume the *first* IRQ / *first* MMIO window.** `_CRS` can list
  several; `Interrupts[0]` is a convention, not a guarantee. Check for the count
  if you support multi-interrupt parts.
- **`_STA` presence checks.** A namespace node can exist but be absent/disabled;
  evaluate `_STA` before trusting a device is really there.
- **No ACPI ⇒ no chain.** On a machine without ACPI, the `bus == "acpi"` node
  never appears, so the `get_driver(parent, …)` matching chain never runs and
  your driver simply never binds. Fail soft (`B_ERROR`/`ENOSYS`), never panic.
- **Trigger/polarity vs. install.** You can compute edge/level and polarity from
  `_CRS`, but `install_io_interrupt_handler` takes plain flags — don't assume the
  APIC was reprogrammed from your computed values; on x86 the platform usually
  already configured the GSI.
- **`walk_resources` runs in the caller's context**, synchronously, once per
  resource. Keep the callback allocation-free; just copy out what you need.
- **Hot events use notify handlers.** For lid/dock/battery events, register with
  `install_notify_handler`; do not poll.
