# How Haiku enumerates and loads drivers

Grounded in `headers/os/drivers/module.h`, `Drivers.h`, `device_manager.h`,
`src/system/kernel/device_manager/`, and
`docs/develop/kernel/device_manager_introduction.rst`.

Everything here is a **kernel add-on**: a shared object under
`add-ons/kernel/` that the kernel loads on demand. What distinguishes the two
driver models is only which symbols the add-on exports.

## 1. The module system (the substrate under both models)

Every kernel add-on is, at bottom, a set of **modules**
(`headers/os/drivers/module.h`). A module is described by:

```cpp
struct module_info {
	const char*	name;		// unique, path-like; suffix is significant
	uint32		flags;		// e.g. B_KEEP_LOADED
	status_t	(*std_ops)(int32 op, ...);	// B_MODULE_INIT / B_MODULE_UNINIT
};
```

An add-on exports a null-terminated array `module_info* modules[]`. It may also
export `module_dependency module_dependencies[]` so the loader resolves other
modules and writes their interface pointers into your statics before your
modules initialize:

```cpp
module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ B_ACPI_MODULE_NAME, (module_info**)&sACPI },	// if you touch ACPI
	{}
};
```

Consumers reach modules with `get_module(name, &info)` / `put_module(name)`,
and enumerate them with `open_module_list()` / `open_module_list_etc(prefix,
suffix)`. The `std_ops` entry point returns `B_OK` for `B_MODULE_INIT` (`1`) and
`B_MODULE_UNINIT` (`2`); return `B_ERROR` for anything else.

The **module name suffix is load-bearing**. The device manager finds drivers by
scanning for modules whose names end in `driver_v1`, and devices by `device_v1`.
Bus managers use their own (`bus_managers/acpi/v1`, and so on).

## 2. The legacy Drivers.h API (BeOS-era)

Defined in `headers/os/drivers/Drivers.h`, loaded by
`src/system/kernel/device_manager/legacy_drivers.cpp`. The add-on exports a
fixed set of C symbols:

```cpp
int32 api_version = B_CUR_DRIVER_API_VERSION;	// == 2

status_t	init_hardware();		// probe; return B_OK if present
status_t	init_driver();			// one-time driver init
void		uninit_driver();
const char**	publish_devices();		// null-terminated /dev paths
device_hooks*	find_device(const char* name);	// hooks for a published path
```

`device_hooks` is the classic function table: `open`, `close`, `free`,
`control`, `read`, `write`, `select`, `deselect`, and the scatter/gather
`readv`/`writev`. The devfs publishes each string from `publish_devices()`, and
routes opens through the matching `device_hooks`.

The legacy API does **no enumeration and no power management** — `init_hardware`
must probe the bus itself. Use it only for a bus-less pseudo-device (think
`/dev/null`-style), or when extending a driver that already uses it. New
hardware drivers should use the device manager.

## 3. The device_manager model (write new drivers this way)

`headers/os/drivers/device_manager.h`. The kernel maintains a tree of
`device_node`s rooted at `get_root_node()`. Bus managers enumerate hardware and
`register_node()` a child per device, tagging it with **attributes**. Your
driver is a `driver_module_info` (name ending `driver_v1`) that the manager
scores against candidate parents.

### 3a. The device node tree and matching

For each node, the manager offers it to every `driver_module_info` via:

```cpp
float supports_device(device_node* parent);
```

Return `0.0` for "not mine"; a higher score wins an ordinary node. Convention:
a generic match ~`0.5`, a specific HID/vendor match `0.6`–`0.8`, an exact model
`0.9`+. Read parent attributes to decide (never assume). For nodes carrying
`B_FIND_MULTIPLE_CHILDREN`, there is intentionally no single winner:
`_RegisterPath()` calls `register_device(parent)` for every positive match.
This is common for I2C peripherals and other simple buses, so support callbacks
must be side-effect-free and coexistence-safe.

### 3b. The driver lifecycle (hooks in call order)

```cpp
struct driver_module_info {
	module_info info;			// name MUST end in "driver_v1"
	float    (*supports_device)(device_node* parent);
	status_t (*register_device)(device_node* parent);
	status_t (*init_driver)(device_node* node, void** _driverCookie);
	void     (*uninit_driver)(void* driverCookie);
	status_t (*register_child_devices)(void* driverCookie);
	status_t (*rescan_child_devices)(void* driverCookie);
	void     (*device_removed)(void* driverCookie);
	status_t (*suspend)(void* driverCookie, int32 state);
	status_t (*resume)(void* driverCookie);
};
```

- `supports_device` → score the parent.
- `register_device` → `register_node(parent, YOUR_DRIVER_MODULE_NAME, attrs,
  ioResources, &node)` to attach yourself to the tree.
- `init_driver(node, &cookie)` → allocate your C++ device object, stash it in
  `*_driverCookie`. This is your "constructor" moment.
- `register_child_devices(cookie)` → call `publish_device(node,
  "class/name", YOUR_DEVICE_MODULE_NAME)` to expose one or more `/dev` entries,
  and/or `register_node()` further children if you are a bus.
- `device_removed` / `uninit_driver` → tear down (respect open handles; see
  cpp-patterns.md for the removal-vs-open refcount dance).
- `suspend`/`resume` exist in the struct but the current kernel **never calls
  them** — do not rely on them for power management.

### 3c. The device (published /dev entry) hooks

```cpp
struct device_module_info {
	module_info info;			// name MUST end in "device_v1"
	status_t (*init_device)(void* driverCookie, void** _deviceCookie);
	void     (*uninit_device)(void* deviceCookie);
	void     (*device_removed)(void* deviceCookie);
	status_t (*open)(void* deviceCookie, const char* path, int openMode,
				void** _cookie);
	status_t (*close)(void* cookie);
	status_t (*free)(void* cookie);
	status_t (*read)(void* cookie, off_t pos, void* buffer, size_t* _length);
	status_t (*write)(void* cookie, off_t pos, const void* buffer,
				size_t* _length);
	status_t (*io)(void* cookie, io_request* request);
	status_t (*control)(void* cookie, uint32 op, void* buffer, size_t length);
	status_t (*select)(void* cookie, uint8 event, selectsync* sync);
	status_t (*deselect)(void* cookie, uint8 event, selectsync* sync);
};
```

`init_device` runs once when the `/dev` entry is created; `open` runs per
`open()` syscall and allocates per-handle state. Use `io` (with an `IORequest`)
for real block/stream DMA; `read`/`write` for simple cases. `control` is your
ioctl surface. Leave unused hooks `NULL`.

### 3d. The device_manager_info API you call

From `headers/os/drivers/device_manager.h` (`B_DEVICE_MANAGER_MODULE_NAME ==
"system/device_manager/v1"`), the methods you will actually use:

```cpp
status_t register_node(parent, moduleName, const device_attr* attrs,
			const io_resource* ioResources, device_node** _node);
status_t publish_device(node, const char* path, const char* deviceModuleName);
status_t get_driver(device_node* node, driver_module_info** _module,
			void** _cookie);		// reach a parent driver's cookie

// Attribute readers — note the trailing `recursive` flag:
status_t get_attr_uint8/16/32/64(const device_node*, const char* name,
			<uintN>* value, bool recursive);
status_t get_attr_string(const device_node*, const char* name,
			const char** _value, bool recursive);
status_t get_attr_raw(const device_node*, const char* name,
			const void** _data, size_t* _size, bool recursive);
status_t get_next_attr(device_node*, device_attr** _attr);

device_node* get_root_node();
device_node* get_parent_node(device_node*);
status_t get_next_child_node(parent, const device_attr* attrs, device_node**);
status_t find_child_node(parent, const device_attr* attrs, device_node**);
void     put_node(device_node*);		// release a node you obtained
```

`recursive == true` walks **up** the tree until the attribute is found — this is
how a child reads an attribute a bus manager set on an ancestor (important for
ACPI; see acpi-layering.md).

For string attributes, `B_OK` does not prove the returned pointer is non-null:
a producer can register a `B_STRING_TYPE` attribute whose value is `NULL`.
Initialize output pointers and require both `B_OK` and a non-null value before
logging or calling `strcmp`.

### 3e. Attributes (device_attr)

A node carries typed attributes (`device_attr`: name, `type_code`, tagged
union). The standard names (all in the header):

- `B_DEVICE_PRETTY_NAME` (string) — human label.
- `B_DEVICE_BUS` (string) — the bus class: `"pci"`, `"acpi"`, `"i2c"`, `"usb"`…
  **This is the primary thing `supports_device` keys on.**
- `B_DEVICE_VENDOR_ID` / `B_DEVICE_ID` (uint16) — PCI-style IDs.
- `B_DEVICE_TYPE` / `B_DEVICE_SUB_TYPE` / `B_DEVICE_INTERFACE` (uint16) — PCI
  class triple.
- `B_DEVICE_UNIQUE_ID` (string), `B_DEVICE_FLAGS` (uint32),
  `B_DEVICE_FIXED_CHILD` (string).

### 3f. Fixed vs dynamic children (bus authors)

If your driver is itself a bus, you choose **one** enumeration style — never mix:

- **Fixed children**: register known children in `register_device` (before
  `register_child_devices`) using `B_DEVICE_FIXED_CHILD` naming the child module.
- **Dynamic children**: set `B_DEVICE_FLAGS` on the node. `B_FIND_CHILD_ON_DEMAND`
  (`0x01`) probes lazily; `B_FIND_MULTIPLE_CHILDREN` (`0x02`) allows several
  drivers to bind; `B_KEEP_DRIVER_LOADED` (`0x04`) pins you.

An "intelligent" bus (PCI) publishes vendor/device/type attrs on each child and
uses `B_FIND_CHILD_ON_DEMAND`; a "simple" bus (ISA) uses
`B_FIND_MULTIPLE_CHILDREN | B_FIND_CHILD_ON_DEMAND` and lets candidates probe.

## 4. Exemplars

- `src/add-ons/kernel/drivers/input/i2c_hid/Driver.cpp` — full modern
  `device_manager` driver; `module_dependencies` + `modules[]` at the tail.
- `src/add-ons/kernel/drivers/disk/virtual/ram_disk/` — a clean device that
  publishes `/dev` entries.
- `src/add-ons/kernel/bus_managers/i2c/` — a bus manager that enumerates and
  registers child nodes.
- `docs/develop/kernel/device_manager_introduction.rst` — the canonical
  narrative; read it once end to end.
