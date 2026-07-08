---
name: haiku-driver-authoring
description: Author a new modern C++ kernel driver for Haiku and introduce it to the codebase. Covers how the kernel enumerates and loads drivers (the legacy Drivers.h API versus the device_manager node tree), the C-ABI-glue-plus-C++-object structure, ACPI enumeration and the _CRS/interrupt layering that gets weird under I2C/SPI/GPIO, kernel synchronization and the ISR/worker split, the RAII toolkit, and Haiku's coding guidelines. Use when creating, structuring, matching, or debugging a Haiku kernel driver — especially ACPI- or I2C-enumerated devices — or when adding one to the Jidō Renga overlay.
---

# Authoring a Haiku kernel driver

This is the "missing guide" for cutting a brand-new modern C++ driver for Haiku
and wiring it into the system. It is grounded in the real headers and the living
drivers in the tree — every struct, constant, and hook named here is verifiable
in `headers/os/drivers/` and `src/add-ons/kernel/`.

Haiku descends from BeOS, and its driver model shows that 90s lineage: a small,
stable C ABI at the boundary, C++ behind it. New drivers should be written in
modern C++ (a C-ABI glue layer plus a real C++ device object), target the
**device_manager** model, and follow Haiku's coding guidelines to the letter.

For the deep material, load the reference files in this skill's `references/`
directory on demand:

- `references/device-model.md` — how the kernel enumerates and loads drivers
  (legacy `Drivers.h` API, the `device_manager` node tree, the module system,
  matching/scoring, publishing `/dev` entries).
- `references/acpi-layering.md` — the ACPI centerpiece: namespace enumeration,
  HID/CID matching, `_CRS`/interrupt extraction, and the three-layer model that
  makes ACPI-under-I2C/SPI/GPIO gross. Read this before touching ACPI.
- `references/cpp-patterns.md` — the C-glue + C++ object structure, cookie
  handoff, kernel synchronization, the ISR/worker split, the RAII toolkit, and
  the `KernelAddon` Jamfile.
- `references/coding-style.md` — Haiku's coding guidelines distilled, the
  tooling (`haiku-format`, `checkstyle.py`), and the Jidō Renga header rule.

## The two driver models

Both models are kernel add-ons built on the same underlying **module system**
(`headers/os/drivers/module.h`: a `module_info` with a `std_ops` entry point,
add-ons export a null-terminated `module_info* modules[]` array).

1. **Legacy `Drivers.h` API** — the BeOS-era interface. The add-on exports
   `api_version`, `init_hardware`, `init_driver`, `publish_devices` (returns
   `const char**` of `/dev` paths), and `find_device` (returns a `device_hooks`
   table: open/close/free/read/write/control/select). Loaded by
   `src/system/kernel/device_manager/legacy_drivers.cpp`. Simple, but it does
   **no** device enumeration or power management for you — you probe hardware
   yourself. Appropriate only for pseudo-devices or when you are maintaining
   something that already uses it.

2. **`device_manager` model** — the modern, recommended path
   (`headers/os/drivers/device_manager.h`). The system maintains a tree of
   `device_node`s. Bus managers (PCI, ACPI, I2C, USB…) enumerate hardware and
   register child nodes with attributes. Your driver publishes a
   `driver_module_info` whose `supports_device()` is scored against candidate
   parent nodes; the best score wins and gets bound. This is how the kernel
   *finds* your hardware for you, handles hot-plug/removal, and lets you sit
   cleanly beneath a bus. **Write new drivers this way.**

Decision rule: if your device is enumerated by any bus (PCI/ACPI/I2C/USB), use
`device_manager`. Reach for the legacy API only for a bus-less pseudo-device, or
to match an existing legacy driver you must extend.

## How loading works, in one breath

The kernel walks the `device_node` tree. For each node it asks every registered
`driver_module_info` for a `float supports_device(parent)` score; `0.0` means
"not mine", higher wins. The winner's `register_device(parent)` calls
`register_node()` to attach itself, then `init_driver(node, &driverCookie)`
brings it up, and `register_child_devices(driverCookie)` calls
`publish_device(node, "class/name", DEVICE_MODULE_NAME)` to expose a `/dev`
entry. Opening that entry runs the `device_module_info` hooks. Module names are
significant: a driver module name **must end in `driver_v1`** and a device
module name in **`device_v1`**. Full detail: `references/device-model.md`.

## The canonical device_manager skeleton

This is the minimal, correct shape of a modern driver. The C-ABI glue is `static`
and thin; all real logic lives in a C++ device object (here elided — see
`references/cpp-patterns.md`). Names and signatures match
`headers/os/drivers/device_manager.h` exactly.

```cpp
#include <device_manager.h>
#include <KernelExport.h>

#include <new>
#include <string.h>


#define FOO_DRIVER_MODULE_NAME "drivers/misc/foo/driver_v1"
#define FOO_DEVICE_MODULE_NAME "drivers/misc/foo/device_v1"

static device_manager_info* sDeviceManager;


// ---- driver_module_info: binds to a parent bus node ---------------------

static float
foo_supports_device(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK)
		return 0.0f;
	if (strcmp(bus, "my_bus") != 0)
		return 0.0f;
	// Inspect vendor/device or ACPI HID here to be sure it is ours.
	return 0.6f;
}


static status_t
foo_register_device(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "Foo Widget" } },
		{ NULL }
	};
	return sDeviceManager->register_node(parent, FOO_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
foo_init_driver(device_node* node, void** _driverCookie)
{
	FooDevice* device = new(std::nothrow) FooDevice(node);
	if (device == NULL)
		return B_NO_MEMORY;
	status_t status = device->InitCheck();
	if (status != B_OK) {
		delete device;
		return status;
	}
	*_driverCookie = device;
	return B_OK;
}


static void
foo_uninit_driver(void* driverCookie)
{
	delete (FooDevice*)driverCookie;
}


static status_t
foo_register_child_devices(void* driverCookie)
{
	FooDevice* device = (FooDevice*)driverCookie;
	return sDeviceManager->publish_device(device->Node(), "misc/foo",
		FOO_DEVICE_MODULE_NAME);
}


// ---- device_module_info: the published /dev entry -----------------------

static status_t
foo_init_device(void* driverCookie, void** _deviceCookie)
{
	*_deviceCookie = driverCookie;	// share the FooDevice
	return B_OK;
}


static status_t
foo_open(void* deviceCookie, const char* path, int openMode, void** _cookie)
{
	*_cookie = deviceCookie;
	return ((FooDevice*)deviceCookie)->Open(openMode);
}


static status_t
foo_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	return ((FooDevice*)cookie)->Read(pos, buffer, _length);
}


static status_t
foo_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	return ((FooDevice*)cookie)->Control(op, buffer, length);
}


static status_t foo_close(void* cookie) { return B_OK; }
static status_t foo_free(void* cookie) { return B_OK; }


// ---- module plumbing ----------------------------------------------------

static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}


static driver_module_info sFooDriverModule = {
	{ FOO_DRIVER_MODULE_NAME, 0, std_ops },
	foo_supports_device,
	foo_register_device,
	foo_init_driver,
	foo_uninit_driver,
	foo_register_child_devices,
	NULL,	// rescan_child_devices
	NULL,	// device_removed
	NULL,	// suspend  (never called by the current kernel)
	NULL,	// resume
};

static device_module_info sFooDeviceModule = {
	{ FOO_DEVICE_MODULE_NAME, 0, NULL },
	foo_init_device,
	NULL,	// uninit_device
	NULL,	// device_removed
	foo_open,
	foo_close,
	foo_free,
	foo_read,
	NULL,	// write
	NULL,	// io
	foo_control,
	NULL,	// select
	NULL,	// deselect
};

module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{}
};

module_info* modules[] = {
	(module_info*)&sFooDriverModule,
	(module_info*)&sFooDeviceModule,
	NULL
};
```

That is a complete, loadable driver. Everything else is filling in `FooDevice`
and (for real hardware) matching + resource acquisition.

## ACPI, in one paragraph (then go read the reference)

ACPI is where the layering gets weird, so it has its own file. The short version:
the ACPI bus manager enumerates the ACPI namespace and registers a node per
device carrying `acpi/*` attributes (`ACPI_DEVICE_HID_ITEM`,
`ACPI_DEVICE_CID_ITEM`, `ACPI_DEVICE_HANDLE_ITEM`, …). A **leaf** ACPI driver
(battery, lid, button) matches `B_DEVICE_BUS == "acpi"` and an exact HID string.
But a device that hangs off an ACPI-described **I2C/SPI/GPIO** controller does
**not** match `bus == "acpi"` — the I2C bus manager copies the ACPI HID/CID and
the `ACPI_DEVICE_HANDLE_ITEM` onto the I2C child, so your driver matches
`bus == "i2c"` **and** reads the ACPI handle off that node to walk `_CRS` for its
interrupt. Getting this wrong is the single most common way an ACPI-enumerated
driver silently fails to bind. Full treatment, with both matching styles and
`_CRS`/IRQ extraction: **`references/acpi-layering.md`**.

## Synchronization, the parts that bite

- An **ISR must never block, sleep, or take a mutex.** Keep it tiny: acknowledge
  the hardware, stash minimal state, wake a worker, return
  `B_HANDLED_INTERRUPT` (or `B_INVOKE_SCHEDULER` if you released a semaphore that
  should preempt).
- The idiomatic pattern is **ISR + worker thread**: the ISR does
  `fEventCV.NotifyOne()` (or `release_sem`) and returns; a `spawn_kernel_thread`
  worker does the real, possibly-blocking work.
- With `ConditionVariable`, add your `ConditionVariableEntry` **before** you
  start the hardware operation, so a fast interrupt can't fire in the gap and be
  missed.

Primitives, headers, the full worker skeleton, and the RAII locker family live
in `references/cpp-patterns.md`; the authoritative kernel doc is
`docs/user/drivers/synchronization_primitives.dox`.

## RAII and kernel utilities

Use Haiku's RAII helpers instead of manual cleanup and `goto`:
`ObjectDeleter`/`ArrayDeleter`/`MemoryDeleter`/`CObjectDeleter` and
`FileDescriptorCloser` from `headers/private/shared/AutoDeleter.h`; the
`MutexLocker`/`RecursiveLocker`/`ReadLocker`/`WriteLocker`/`InterruptsSpinLocker`
lockers from `headers/private/kernel/util/AutoLock.h`; and
`BReference`/`BReferenceable` for refcounted objects. Prefer early-return + RAII
over cleanup labels. Kernel facilities (`dprintf`, `snooze`,
`spawn_kernel_thread`, `install_io_interrupt_handler`, `map_physical_memory`) are
in `headers/os/drivers/KernelExport.h`. Details in `references/cpp-patterns.md`.

## Coding standards (non-negotiable)

Haiku has a strict, enforced style. The essentials: tabs to indent (a tab is 4
columns), 100-column lines, `InterCaps` for types/functions, `interCaps` for
variables, and the sigils `fMember`, `gGlobal`, `sStatic`, `kConstant`,
`_privateMethod`. Use `SupportDefs.h` types (`int32`, `status_t`, `off_t`), C++
casts, `NULL` (not `0`), no `else` after `return`, no `goto`, two blank lines
between functions. Format with `haiku-format` and check with
`src/tools/checkstyle/checkstyle.py`. Full distillation and the project header
rule: `references/coding-style.md`. Authoritative source:
<https://www.haiku-os.org/development/coding-guidelines>.

## Weaving it into the overlay

This skill covers *authoring* the driver. To compile and integrate it — the
`KernelAddon` Jamfile, the Jam-native graft into the captive Haiku checkout, and
the build recipe — use the companion **`jido-renga-overlay-build`** skill and the
root `AGENTS.md`. In brief: overlay sources live under `overlay/drivers/<class>/`
with a `Jamfile` calling `KernelAddon`, and `tools/weave` grafts them into the
captive build; you never edit the captive.

## Authoritative sources and exemplars

Docs:

- Coding guidelines — <https://www.haiku-os.org/development/coding-guidelines>
- Writing drivers (API topics) — <https://www.haiku-os.org/docs/api/drivers.html>
- `docs/develop/kernel/device_manager_introduction.rst` — the canonical
  `device_manager` narrative.
- `docs/user/drivers/synchronization_primitives.dox` — kernel synchronization.

Headers to read directly:

- `headers/os/drivers/device_manager.h`, `module.h`, `Drivers.h`,
  `KernelExport.h`, `ACPI.h`
- `headers/private/kernel/util/AutoLock.h`,
  `headers/private/shared/AutoDeleter.h`,
  `headers/private/kernel/lock.h`, `condition_variable.h`

In-tree exemplars (read the closest analog to what you are building):

- `src/add-ons/kernel/drivers/input/i2c_hid/` — I2C + `device_manager`, the
  closest upstream analog to a modern touch/HID driver.
- `src/add-ons/kernel/drivers/power/acpi_battery/`, `acpi_button/`, `acpi_lid/` —
  leaf ACPI drivers matching `bus == "acpi"` + HID.
- `src/add-ons/kernel/busses/i2c/pch/pch_i2c_acpi.cpp` and
  `src/add-ons/kernel/bus_managers/i2c/I2CBus.cpp` — how ACPI attributes are
  plumbed onto I2C children.
- `src/add-ons/kernel/drivers/disk/mmc/sdhci/sdhci.cpp` — canonical worker-driven
  interrupt handling.

Jidō Renga overlay exemplars:

- `overlay/drivers/input/i2c_atmel_mxt/` — I2C + ACPI-handle matching, a C++
  device object, and the ISR/ConditionVariable worker split. The reference
  implementation of everything in this skill.
- `overlay/drivers/input/cros_ec_keyboard/` — driver-cookie-is-the-object shape.
- `overlay/bus_managers/iosf_mbi/` — a singleton bus manager.
