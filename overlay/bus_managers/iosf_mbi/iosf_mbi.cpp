/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 */

/*
 * Intel IOSF-MBI (On-Chip System Fabric Message Buffer Interface) driver.
 *
 * Provides sideband register access on BayTrail/CherryTrail Atom platforms
 * via PCI configuration space. Used by other drivers (SDHCI, audio, thermal)
 * to configure chipset errata workarounds.
 */

#include <common/iosf_mbi.h>


#include <KernelExport.h>
#include <new>
#include <PCI.h>
#include <string.h>


#define TRACE_IOSF_MBI
#ifdef TRACE_IOSF_MBI
	#define TRACE(x...) dprintf("\33[34miosf_mbi:\33[0m " x)
#else
	#define TRACE(x...) ;
#endif
#define ERROR(x...) dprintf("\33[34miosf_mbi:\33[0m " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


// ============================================================================
// PCI config space offsets for IOSF-MBI message buffer protocol
// ============================================================================

enum {
	kMcrOffset		= 0xd0,	// Message Control Register
	kMdrOffset		= 0xd4,	// Message Data Register
	kMcrxOffset		= 0xd8,	// Message Control Register Extended
};

static const uint32 kMbiEnableFlag	= 0xf0;


// ============================================================================
// BayTrail PCI device IDs that expose the IOSF-MBI config space interface.
// Ordered by preference: PMC first (power control unit hosts sideband regs),
// then host bridge, then individual SDHCI controllers.
// ============================================================================

struct BayTrailDevice {
	uint16		vendor;
	uint16		device;
	const char*	name;
};

static const BayTrailDevice kSupportedDevices[] = {
	{ 0x8086, 0x0f1c, "BayTrail Power Control Unit" },
	{ 0x8086, 0x0f00, "BayTrail SoC Transaction Register" },
	{ 0x8086, 0x0f14, "BayTrail SDHCI eMMC" },
	{ 0x8086, 0x0f15, "BayTrail SDHCI SDIO" },
	{ 0x8086, 0x0f16, "BayTrail SDHCI SD" },
};


// ============================================================================
// IosfMbiBus -- singleton that owns the PCI BDF and performs register access
// ============================================================================

class IosfMbiBus {
public:
	IosfMbiBus();
	~IosfMbiBus();

	status_t	ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
				uint32_t* value) const;
	status_t	WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
				uint32_t value) const;

	pci_module_info*	fPci;
	uint8				fBus;
	uint8				fDevice;
	uint8				fFunction;

private:
	uint32			_FormMcr(uint8_t opcode, uint8_t port,
						uint8_t offsetLo) const;
	status_t			_PciReadMdr(uint32 mcrx, uint32 mcr,
						uint32* mdr) const;
	status_t			_PciWriteMdr(uint32 mcrx, uint32 mcr,
						uint32 mdr) const;
};


IosfMbiBus::IosfMbiBus()
{
	CALLED();
	fPci = nullptr;
	fBus = 0;
	fDevice = 0;
	fFunction = 0;
}


IosfMbiBus::~IosfMbiBus()
{
}


static status_t
sInit(IosfMbiBus* bus)
{
	CALLED();

	if (get_module(B_PCI_MODULE_NAME, (module_info**)&bus->fPci) != B_OK) {
		TRACE("PCI module not available\n");
		return B_ERROR;
	}

	pci_info info;
	for (long i = 0; bus->fPci->get_nth_pci_info(i, &info) == B_OK; i++) {
		for (size_t j = 0; j < B_COUNT_OF(kSupportedDevices); j++) {
			if (info.vendor_id == kSupportedDevices[j].vendor
				&& info.device_id == kSupportedDevices[j].device) {
				bus->fBus = info.bus;
				bus->fDevice = info.device;
				bus->fFunction = info.function;
				TRACE("bound to %s at %d:%d.%d\n",
					kSupportedDevices[j].name,
					bus->fBus, bus->fDevice, bus->fFunction);
				return B_OK;
			}
		}
	}

	TRACE("no BayTrail PCI device found\n");
	put_module(B_PCI_MODULE_NAME);
	bus->fPci = nullptr;
	return B_ERROR;
}


uint32
IosfMbiBus::_FormMcr(uint8_t opcode, uint8_t port, uint8_t offsetLo) const
{
	return ((uint32)opcode << 24)
		| ((uint32)port << 16)
		| ((uint32)offsetLo << 8)
		| kMbiEnableFlag;
}


status_t
IosfMbiBus::_PciReadMdr(uint32 mcrx, uint32 mcr, uint32* mdr) const
{
	if (fPci == nullptr)
		return B_ERROR;

	if (mcrx != 0)
		fPci->write_pci_config(fBus, fDevice, fFunction, kMcrxOffset, 4, mcrx);

	fPci->write_pci_config(fBus, fDevice, fFunction, kMcrOffset, 4, mcr);

	*mdr = fPci->read_pci_config(fBus, fDevice, fFunction, kMdrOffset, 4);

	return B_OK;
}


status_t
IosfMbiBus::_PciWriteMdr(uint32 mcrx, uint32 mcr, uint32 mdr) const
{
	if (fPci == nullptr)
		return B_ERROR;

	fPci->write_pci_config(fBus, fDevice, fFunction, kMdrOffset, 4, mdr);

	if (mcrx != 0)
		fPci->write_pci_config(fBus, fDevice, fFunction, kMcrxOffset, 4, mcrx);

	fPci->write_pci_config(fBus, fDevice, fFunction, kMcrOffset, 4, mcr);

	return B_OK;
}


status_t
IosfMbiBus::ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
		uint32_t* value) const
{
	if (fPci == nullptr)
		return B_ERROR;

	uint32 mcr = _FormMcr(opcode, port, static_cast<uint8>(offset & 0xff));
	uint32 mcrx = offset & 0xffffff00;

	return _PciReadMdr(mcrx, mcr, value);
}


status_t
IosfMbiBus::WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
		uint32_t value) const
{
	if (fPci == nullptr)
		return B_ERROR;

	uint32 mcr = _FormMcr(opcode, port, static_cast<uint8>(offset & 0xff));
	uint32 mcrx = offset & 0xffffff00;

	return _PciWriteMdr(mcrx, mcr, value);
}


// ============================================================================
// Module interface -- exports function pointers via iosf_mbi_module_info
// ============================================================================

static IosfMbiBus* gIosfMbiBus = nullptr;


static status_t
iosf_mbi_read_fn(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t* value)
{
	if (gIosfMbiBus == nullptr)
		return B_ERROR;

	return gIosfMbiBus->ReadRegister(port, opcode, offset, value);
}


static status_t
iosf_mbi_write_fn(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t value)
{
	if (gIosfMbiBus == nullptr)
		return B_ERROR;

	return gIosfMbiBus->WriteRegister(port, opcode, offset, value);
}


// ============================================================================
// Module entry points
// ============================================================================

static int32
iosf_mbi_module_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return gIosfMbiBus != nullptr ? B_OK : B_ERROR;

		case B_MODULE_UNINIT:
			delete gIosfMbiBus;
			gIosfMbiBus = nullptr;
			return B_OK;

		default:
			return B_BAD_VALUE;
	}
}


static iosf_mbi_module_info sIosfMbiModule = {
	{
		B_IOSF_MBI_MODULE_NAME,
		0,
		iosf_mbi_module_ops
	},
	.read  = iosf_mbi_read_fn,
	.write = iosf_mbi_write_fn,
};


module_info* modules[] = {
	& sIosfMbiModule.info,
	NULL
};


extern "C"
status_t
init_module(void)
{
	CALLED();

	// Always succeed — PCI device discovery is lazy.
	// Read/write ops return B_ERROR if no BayTrail PCI device is present.
	gIosfMbiBus = new(std::nothrow) IosfMbiBus();
	if (gIosfMbiBus == nullptr) {
		ERROR("failed to allocate IosfMbiBus\n");
		return B_NO_MEMORY;
	}

	sInit(gIosfMbiBus);

	TRACE("IOSF-MBI module ready\n");
	return B_OK;
}


extern "C"
void
uninit_module(void)
{
	CALLED();

	if (gIosfMbiBus != nullptr && gIosfMbiBus->fPci != nullptr) {
		put_module(B_PCI_MODULE_NAME);
	}
	delete gIosfMbiBus;
	gIosfMbiBus = nullptr;
}
