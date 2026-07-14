# Modern C++ driver structure, concurrency, and RAII

How to actually build the C++ behind the C-ABI glue. Grounded in
`headers/os/drivers/KernelExport.h`, `headers/private/kernel/util/AutoLock.h`,
`headers/private/shared/AutoDeleter.h`,
`headers/private/kernel/condition_variable.h`, and the overlay drivers.

## The C-glue + C++ object split

The module boundary is a stable **C ABI** (the `driver_module_info` /
`device_module_info` function tables). Everything behind it is real C++. The
pattern is: thin `static` C functions that immediately delegate to a C++ object.

The three cookies map cleanly to lifetimes:

| Cookie          | Created in            | Lives as             | Freed in            |
|-----------------|-----------------------|----------------------|---------------------|
| `driverCookie`  | `init_driver`         | your `Device` object | `uninit_driver`     |
| `deviceCookie`  | `init_device`         | usually == driverCookie | `uninit_device`  |
| open `cookie`   | `open`                | per-handle state     | `free`              |

So `init_driver` is your constructor site and `uninit_driver` your destructor
site:

```cpp
static status_t
foo_init_driver(device_node* node, void** _driverCookie)
{
	FooDevice* device = new(std::nothrow) FooDevice(node);
	if (device == NULL)
		return B_NO_MEMORY;
	status_t status = device->InitCheck();
	if (status != B_OK) {
		delete device;			// early-return + delete, no goto
		return status;
	}
	*_driverCookie = device;
	return B_OK;
}
```

Note `new(std::nothrow)` — the kernel has no C++ exceptions, so never rely on a
throwing `new`. The `InitCheck()` idiom (constructor stays trivial, a separate
method reports readiness) is pervasive in Haiku; use it.

### The removal-vs-open dance

A device can be physically removed while a handle is still open. Don't delete the
object out from under an open fd. The established pattern (see
`overlay/.../i2c_atmel_mxt/Driver.cpp`): `device_removed` flags the object;
`free` (last close) checks "removed && not open" under the driver lock and only
then deletes:

```cpp
static status_t
foo_free(void* _cookie)
{
	FooCookie* cookie = (FooCookie*)_cookie;
	MutexLocker locker(sDriverLock);
	if (cookie->device->IsRemoved() && !cookie->device->IsOpen())
		delete cookie->device;
	delete cookie;
	return B_OK;
}
```

## Concurrency

### Primitives and where they live

| Primitive           | Header                         | ISR-safe?         | Use for                          |
|---------------------|--------------------------------|-------------------|----------------------------------|
| `atomic_*`          | `SupportDefs.h`                | yes               | counters, flags, lock-free state |
| `spinlock`          | `KernelExport.h` / `lock.h`    | yes (busy-wait)   | very short critical sections     |
| `mutex`             | `headers/private/kernel/lock.h`| **no** (blocks)   | general mutual exclusion         |
| `recursive_lock`    | `lock.h`                       | no                | re-entrant paths                 |
| `rw_lock`           | `lock.h`                       | no                | many readers / rare writers      |
| semaphore (`create_sem`) | `OS.h`                    | release only      | classic ISR→thread signaling     |
| `ConditionVariable` | `condition_variable.h`         | `NotifyOne/All` ok| modern ISR→worker wakeups        |

### ISR rules (memorize)

An interrupt handler runs in interrupt context. It **must not** block, sleep,
`snooze`, take a `mutex`, or allocate. Keep it to the minimum required by the
controller contract: establish whether the shared line may be yours, snapshot
or acknowledge only the required status, signal a worker, and return. Do not
make “the ISR always owns acknowledgement” a generic rule; some hardware needs
immediate W1C while other designs safely treat the interrupt as an unordered
hint and converge in the worker. The return value
(`headers/os/drivers/KernelExport.h`):

- `B_UNHANDLED_INTERRUPT` (`0`) — not mine, pass on.
- `B_HANDLED_INTERRUPT` (`1`) — mine, done.
- `B_INVOKE_SCHEDULER` (`2`) — mine, and I woke something that should preempt.

### The ISR + worker pattern (the one to copy)

Do the real work on a kernel thread; the ISR only wakes it. With
`ConditionVariable`, add the entry **before** enabling/triggering the hardware,
so an interrupt firing in the gap cannot be missed:

```cpp
// setup (once):
fEventCV.Init(this, "foo event");
fWorker = spawn_kernel_thread(_WorkerEntry, "foo worker",
	B_NORMAL_PRIORITY, this);
resume_thread(fWorker);
install_io_interrupt_handler(fIRQ, _InterruptEntry, this, 0);

// ISR — tiny, non-blocking:
int32
FooDevice::_InterruptEntry(void* data)
{
	FooDevice* self = (FooDevice*)data;
	self->_AckHardware();
	self->fEventCV.NotifyOne();		// wake the worker
	return B_HANDLED_INTERRUPT;
}

// worker — may block, take mutexes, do I/O:
status_t
FooDevice::_Worker()
{
	while (!fStopping) {
		ConditionVariableEntry entry;
		fEventCV.Add(&entry);		// register before we could miss a wake
		entry.Wait(B_RELATIVE_TIMEOUT, 100000 /* 100ms */);
		_DrainAndProcess();
	}
	return B_OK;
}
```

`ConditionVariable::Wait(uint32 flags, bigtime_t timeout)` is the add+wait
convenience; the explicit `Add(&entry)` then `entry.Wait()` form shown above is
what you want when a wake can race the arming of the wait.

## The RAII toolkit (don't hand-roll cleanup)

`headers/private/shared/AutoDeleter.h`:

- `ObjectDeleter<T>` — `delete`s a `new`ed object at scope exit.
- `ArrayDeleter<T>` — `delete[]`s an array.
- `MemoryDeleter` — `free()`s `malloc`ed memory.
- `CObjectDeleter<T, R, func>` — calls an arbitrary destroy function.
- `FileDescriptorCloser` — `close()`s an fd.

Call `.Detach()` to release ownership when you hand the pointer onward.

`headers/private/kernel/util/AutoLock.h` (scoped locks):

- `MutexLocker`, `MutexTryLocker`, `RecursiveLocker`
- `ReadLocker`, `WriteLocker` (for `rw_lock`)
- `SpinLocker`, `InterruptsSpinLocker` (disables interrupts + takes a spinlock)

`headers/os/support/Referenceable.h`: derive shared objects from
`BReferenceable` and hold them with `BReference<T>` for refcounted lifetime.

Idiom: **early-return + RAII beats cleanup labels.** Haiku style forbids `goto`;
let deleters and lockers unwind for you.

```cpp
status_t
FooDevice::DoThing()
{
	MutexLocker locker(fLock);			// released on every return path
	ObjectDeleter<Buffer> buffer(new(std::nothrow) Buffer(size));
	if (!buffer.IsSet())
		return B_NO_MEMORY;
	// ...use buffer...
	buffer.Detach();				// hand ownership onward if needed
	return B_OK;
}
```

## Kernel utilities you will reach for

From `headers/os/drivers/KernelExport.h`:

- Logging: `dprintf()` (serial/syslog), `panic()` (last resort only).
- Timing: `snooze(bigtime_t)` (thread context only — never in an ISR).
- Threads: `spawn_kernel_thread(func, name, priority, arg)` + `resume_thread`.
- Interrupts: `install_io_interrupt_handler` / `remove_io_interrupt_handler`.
- MMIO: `map_physical_memory("name", physAddr, size, B_ANY_KERNEL_ADDRESS,
  B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &virtAddr)` returns an `area_id`;
  pair it with `delete_area` (or an `area_id` RAII wrapper) on teardown.
- DMA/memory: `get_memory_map`, `lock_memory` / `unlock_memory`.

Kernel containers live under `headers/private/kernel/util/`
(`DoublyLinkedList`, `Vector`, `AVLTree`) — prefer them to STL in kernel code.

## Building it

Each driver is a `KernelAddon` in a `Jamfile`. Overlay shape (real, from
`overlay/drivers/input/i2c_atmel_mxt/Jamfile`):

```jam
SubDir JIDO_RENGA_TOP overlay drivers input i2c_atmel_mxt ;

SubDirC++Flags -fno-rtti ;			# kernel C++: no RTTI, no exceptions

SubDirSysHdrs $(HAIKU_TOP) headers os drivers ;
UsePrivateHeaders [ FDirName kernel util ] input drivers device i2c ;
UsePrivateKernelHeaders ;

KernelAddon i2c_atmel_mxt :
	Driver.cpp
	MxtDevice.cpp
;
```

Header directories resolve against the captive Haiku submodule via `HAIKU_TOP`;
the add-on itself lives under `JIDO_RENGA_TOP`. For the full graft/weave/build
recipe use the **`jido-renga-overlay-build`** skill. In-tree drivers use the same
`KernelAddon` call under `HAIKU_TOP src add-ons kernel drivers <class> <name>`.
