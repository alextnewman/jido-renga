// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

/*
 * Intel IOSF-MBI sideband register access for Bay Trail and Cherry Trail.
 *
 * MCR, MDR, and MCRX exist only in the SoC transaction router at PCI 0:0.0;
 * accesses to other functions are invalid. Initialization uses the module
 * std_ops hook and resolves early ACPI boot ordering through formal
 * device-manager and x86 PCI-controller dependencies. All configuration
 * traffic goes through Haiku's PCI bus manager.
 */

#include <common/iosf_mbi.h>
#include <common/Trace.h>

#include "IosfMbiProtocol.h"

#include <KernelExport.h>
#include <PCI.h>
#include <device_manager.h>
#include <util/AutoLock.h>

#include <new>
#include <string.h>


#define TRACE_IOSF_MBI
#define IOSF_MBI_TRACE_LABEL "iosf_mbi"
#ifdef TRACE_IOSF_MBI
#	define TRACE(x...) JR_DIAG_TRACE(IOSF_MBI_TRACE_LABEL, x)
#else
#	define TRACE(x...) JR_DIAG_DISABLED()
#endif
#define ERROR(x...) JR_DIAG_ERROR(IOSF_MBI_TRACE_LABEL, x)


namespace {


pci_module_info* gPci = nullptr;
device_manager_info* gDeviceManager = nullptr;
driver_module_info* gX86PciController = nullptr;

constexpr char kX86PciControllerModuleName[]
	= "busses/pci/x86/driver_v1";


// Supported transaction routers. IOSF-MBI registers are valid only at 0:0.0.
const char*
HostBridgeName(uint16 device)
{
	switch (device) {
		case jr::iosf::kBayTrailHostBridgeId:
			return "Bay Trail SoC transaction router";
		case jr::iosf::kCherryTrailHostBridgeId:
			return "Cherry Trail SoC transaction router";
		default:
			return "unknown host bridge";
	}
}


// Owns the PCI location of the message-bus host bridge and performs the two-
// (or three-) step MCR/MDR handshake. One per system.
class IosfMbiBus {
public:
	IosfMbiBus() { mutex_init(&fLock, "iosf mbi transaction"); }
	~IosfMbiBus() { mutex_destroy(&fLock); }

	status_t	Discover();

	status_t	ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
					uint32_t* value);
	status_t	WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
					uint32_t value);

private:
	status_t	_ProbeHostBridge();
	status_t	_RegisterPciController();

	uint8				fBus = 0;
	uint8				fDevice = 0;
	uint8				fFunction = 0;
	bool				fDiscovered = false;
	mutable mutex		fLock;
};


status_t
IosfMbiBus::_ProbeHostBridge()
{
	if (gPci == nullptr)
		return B_NO_INIT;

	const uint32 id = gPci->read_pci_config(0, 0, 0, PCI_vendor_id, 4);
	if (id == UINT32_MAX)
		return B_NO_INIT;

	const uint16 vendor = id & 0xffffu;
	const uint16 device = id >> 16;
	if (!jr::iosf::IsSupportedHostBridge(vendor, device)) {
		ERROR("PCI 0:0.0 is %04x:%04x, not a supported IOSF-MBI host\n",
			vendor, device);
		return B_DEVICE_NOT_FOUND;
	}

	fBus = 0;
	fDevice = 0;
	fFunction = 0;
	fDiscovered = true;
	TRACE("bound to %s at 0:0.0\n", HostBridgeName(device));
	return B_OK;
}


status_t
IosfMbiBus::_RegisterPciController()
{
	if (gDeviceManager == nullptr || gX86PciController == nullptr
		|| gX86PciController->register_device == nullptr) {
		return B_NO_INIT;
	}

	device_node* root = gDeviceManager->get_root_node();
	if (root == nullptr)
		return B_NO_INIT;

	status_t status = gX86PciController->register_device(root);
	gDeviceManager->put_node(root);

	if (status == B_NAME_IN_USE)
		return B_OK;
	if (status != B_OK) {
		ERROR("early x86 PCI controller registration failed: %s\n",
			strerror(status));
		return status;
	}

	TRACE("registered x86 PCI controller ahead of ACPI boot-media probing\n");
	return B_OK;
}


status_t
IosfMbiBus::Discover()
{
	if (fDiscovered)
		return B_OK;

	status_t status = _ProbeHostBridge();
	if (status != B_NO_INIT)
		return status;

	TRACE("PCI domain not registered yet; starting it through device_manager\n");
	status = _RegisterPciController();
	if (status != B_OK)
		return status;

	status = _ProbeHostBridge();
	if (status == B_NO_INIT) {
		ERROR("x86 PCI controller registered without a usable domain\n");
		return B_NO_INIT;
	}
	return status;
}


status_t
IosfMbiBus::ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
	uint32_t* value)
{
	if (value == nullptr)
		return B_ERROR;
	MutexLocker locker(fLock);
	status_t status = Discover();
	if (status != B_OK)
		return status;

	const uint32_t mcr = jr::iosf::FormMcr(opcode, port, offset);
	const uint32_t mcrx = jr::iosf::McrxFor(offset);

	// Always write MCRX, including zero, so a prior extended-offset access cannot
	// leak its upper address bits into this transaction.
	gPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrxOffset, 4,
		mcrx);
	gPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrOffset, 4,
		mcr);
	*value = gPci->read_pci_config(fBus, fDevice, fFunction,
		jr::iosf::kMdrOffset, 4);
	return B_OK;
}


status_t
IosfMbiBus::WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
	uint32_t value)
{
	MutexLocker locker(fLock);
	status_t status = Discover();
	if (status != B_OK)
		return status;

	const uint32_t mcr = jr::iosf::FormMcr(opcode, port, offset);
	const uint32_t mcrx = jr::iosf::McrxFor(offset);

	// Data first, then the extended offset, then the trigger (MCR) last --
	// writing MCR is what actually launches the transaction.
	gPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMdrOffset, 4,
		value);
	gPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrxOffset, 4,
		mcrx);
	gPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrOffset, 4,
		mcr);
	return B_OK;
}


IosfMbiBus* gBus = nullptr;


} // unnamed namespace


// ---- Module interface ------------------------------------------------------

static status_t
iosf_mbi_read_fn(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t* value)
{
	if (gBus == nullptr)
		return B_NO_INIT;
	return gBus->ReadRegister(port, opcode, offset, value);
}


static status_t
iosf_mbi_write_fn(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t value)
{
	if (gBus == nullptr)
		return B_NO_INIT;
	return gBus->WriteRegister(port, opcode, offset, value);
}


// IOSF is a mandatory BSP facility. Initialization does not succeed until the
// host bridge is reachable through Haiku's registered PCI controller.
static status_t
iosf_mbi_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			IosfMbiBus* bus = new(std::nothrow) IosfMbiBus();
			if (bus == nullptr) {
				ERROR("out of memory\n");
				return B_NO_MEMORY;
			}
			status_t status = bus->Discover();
			if (status != B_OK) {
				delete bus;
				return status;
			}
			gBus = bus;
			TRACE("ready\n");
			return B_OK;
		}

		case B_MODULE_UNINIT:
			if (gBus != nullptr) {
				delete gBus;
				gBus = nullptr;
			}
			return B_OK;

		default:
			return B_BAD_VALUE;
	}
}


static iosf_mbi_module_info sIosfMbiModule = {
	{
		B_IOSF_MBI_MODULE_NAME,
		0,
		iosf_mbi_std_ops
	},
	.read  = iosf_mbi_read_fn,
	.write = iosf_mbi_write_fn,
};


module_info* modules[] = {
	(module_info*)&sIosfMbiModule,
	NULL
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_PCI_MODULE_NAME, (module_info**)&gPci },
	{ kX86PciControllerModuleName, (module_info**)&gX86PciController },
	{}
};
