// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

/*
 * Intel IOSF-MBI (On-Chip System Fabric, Message Buffer Interface) bus manager.
 *
 * Sideband register access on BayTrail / CherryTrail Atom SoCs. Other drivers
 * (SDHCI OCP fixup, audio, thermal) borrow it to poke chipset errata registers
 * that have no MMIO aperture.
 *
 * This is a clean-room rewrite, cross-checked against Linux
 * (arch/x86/platform/intel/iosf_mbi.c), NOT a port of the earlier draft. Two
 * things the draft got wrong are fixed here:
 *
 *   1. Initialization ran from free `init_module()` / `uninit_module()`
 *      functions -- a Linux-ism. Haiku never calls those, so the singleton was
 *      never created and B_MODULE_INIT always reported failure (the module was
 *      dead on arrival). Discovery now happens in the real std_ops hook.
 *
 *   2. It bound to whichever BayTrail PCI function it saw first (PMC, or even an
 *      SDHCI controller). The message-bus registers (MCR/MDR/MCRX at 0xD0..0xD8)
 *      live ONLY in the SoC transaction router / host bridge config space
 *      (0:0:0). Writing 0xD0 on any other function is not a sideband access --
 *      at best a no-op, at worst poking a real register. We now bind strictly to
 *      the host bridge (BYT 8086:0F00, CHT 8086:2280), matching Linux's
 *      iosf_mbi_pci_ids.
 */

#include <common/iosf_mbi.h>

#include "IosfMbiProtocol.h"

#include <KernelExport.h>
#include <PCI.h>

#include <new>


#define TRACE_IOSF_MBI
#ifdef TRACE_IOSF_MBI
	#define TRACE(x...) dprintf("\33[34miosf_mbi:\33[0m " x)
#else
	#define TRACE(x...) ;
#endif
#define ERROR(x...) dprintf("\33[34miosf_mbi:\33[0m " x)


namespace {


// The ONLY PCI functions that host the IOSF-MBI message-bus registers: the SoC
// transaction router (a.k.a. host bridge / "SSA-CUnit"), always at 0:0:0. This
// list is deliberately narrow -- see the file header for why the PMC and the
// SDHCI controllers must NOT appear here.
struct HostBridgeId {
	uint16		vendor;
	uint16		device;
	const char*	name;
};

const HostBridgeId kMessageBusHosts[] = {
	{ 0x8086, 0x0f00, "BayTrail SoC transaction router" },
	{ 0x8086, 0x2280, "CherryTrail SoC transaction router" },
};

constexpr size_t kMessageBusHostCount =
	sizeof(kMessageBusHosts) / sizeof(kMessageBusHosts[0]);


// Owns the PCI location of the message-bus host bridge and performs the two-
// (or three-) step MCR/MDR handshake. One per system.
class IosfMbiBus {
public:
	status_t	Discover();

	status_t	ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
					uint32_t* value) const;
	status_t	WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
					uint32_t value) const;

	pci_module_info*	Pci() const { return fPci; }

private:
	pci_module_info*	fPci = nullptr;
	uint8				fBus = 0;
	uint8				fDevice = 0;
	uint8				fFunction = 0;
};


status_t
IosfMbiBus::Discover()
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&fPci) != B_OK) {
		TRACE("PCI module not available\n");
		return B_ERROR;
	}

	pci_info info;
	for (long i = 0; fPci->get_nth_pci_info(i, &info) == B_OK; i++) {
		for (size_t j = 0; j < kMessageBusHostCount; j++) {
			if (info.vendor_id != kMessageBusHosts[j].vendor
				|| info.device_id != kMessageBusHosts[j].device) {
				continue;
			}
			fBus = info.bus;
			fDevice = info.device;
			fFunction = info.function;
			TRACE("bound to %s at %d:%d.%d\n", kMessageBusHosts[j].name,
				fBus, fDevice, fFunction);
			return B_OK;
		}
	}

	TRACE("no IOSF-MBI host bridge present; module inert\n");
	put_module(B_PCI_MODULE_NAME);
	fPci = nullptr;
	return B_ENTRY_NOT_FOUND;
}


status_t
IosfMbiBus::ReadRegister(uint8_t port, uint8_t opcode, uint32_t offset,
	uint32_t* value) const
{
	if (fPci == nullptr || value == nullptr)
		return B_ERROR;

	const uint32_t mcr = jr::iosf::FormMcr(opcode, port, offset);
	const uint32_t mcrx = jr::iosf::McrxFor(offset);

	if (mcrx != 0) {
		fPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrxOffset,
			4, mcrx);
	}
	fPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrOffset, 4,
		mcr);
	*value = fPci->read_pci_config(fBus, fDevice, fFunction,
		jr::iosf::kMdrOffset, 4);
	return B_OK;
}


status_t
IosfMbiBus::WriteRegister(uint8_t port, uint8_t opcode, uint32_t offset,
	uint32_t value) const
{
	if (fPci == nullptr)
		return B_ERROR;

	const uint32_t mcr = jr::iosf::FormMcr(opcode, port, offset);
	const uint32_t mcrx = jr::iosf::McrxFor(offset);

	// Data first, then the extended offset, then the trigger (MCR) last --
	// writing MCR is what actually launches the transaction.
	fPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMdrOffset, 4,
		value);
	if (mcrx != 0) {
		fPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrxOffset,
			4, mcrx);
	}
	fPci->write_pci_config(fBus, fDevice, fFunction, jr::iosf::kMcrOffset, 4,
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


// The real Haiku module lifecycle. Discovery lives here (NOT in a free
// init_module) so the module is "available" via get_module ONLY once it has a
// message-bus host bridge to talk to.
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
				if (gBus->Pci() != nullptr)
					put_module(B_PCI_MODULE_NAME);
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
