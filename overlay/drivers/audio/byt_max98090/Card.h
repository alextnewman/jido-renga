// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <PCI.h>
#include <device_manager.h>
#include <i2c.h>
#include <lock.h>
#include <hmulti_audio.h>

#include <common/Gpio.h>


namespace jr::byt_audio {

class Card {
public:
	Card();
	~Card();

	status_t AttachLpe(device_node* acpiNode);
	void DetachLpe();
	status_t AttachCodec(device_node* codecNode,
		i2c_device_interface* interface, i2c_device cookie);
	void DetachCodec(i2c_device cookie);

	bool Ready();
	status_t Open();
	status_t Close();
	status_t Control(uint32 op, void* buffer, size_t length);

private:
	status_t _InitializeLpe(device_node* acpiNode);
	status_t _EnablePlatformClock();
	status_t _MapLpeResources(device_node* acpiNode);
	status_t _LoadAndStartFirmware();
	status_t _LoadFirmwareFile(uint8** data, size_t* size);
	status_t _InitializeCodec();
	status_t _WriteCodec(uint8 reg, uint8 value);
	status_t _ReadCodec(uint8 reg, uint8& value);
	status_t _SetCodecVolume(uint8 volume, bool muted);
	status_t _SetHeadphoneVolume(uint8 volume, bool muted);
	status_t _InitializeJackDetection();
	void _TeardownJackDetection();
	status_t _ApplyJackState(bool headphonePresent, bool microphonePresent);
	static void _HeadphoneEvent(void* context, const gpio::Event& event);
	static void _MicrophoneEvent(void* context, const gpio::Event& event);
	void _UnmapLpe();

	status_t _GetDescription(multi_description* description);
	status_t _GetEnabledChannels(multi_channel_enable* enable);
	status_t _SetEnabledChannels(multi_channel_enable* enable);
	status_t _GetGlobalFormat(multi_format_info* format);
	status_t _SetGlobalFormat(multi_format_info* format);
	status_t _ListMixControls(multi_mix_control_info* controls);
	status_t _GetMix(multi_mix_value_info* values);
	status_t _SetMix(multi_mix_value_info* values);
	status_t _GetBuffers(multi_buffer_list* buffers);
	status_t _BufferExchange(multi_buffer_info* info);
	status_t _ForceStop();

	status_t _IpcSend(uint8 ipcMsg, uint8 taskId, const void* data,
				uint32 length, bool responseRequired, void* response,
				uint32 responseCapacity, uint32* responseSize);
	status_t _IpcSendByteStream(uint8 ipcMsg, uint8 taskId,
				const void* data, uint16 length, bool blocked);
	status_t _IpcSendAllocate(uint8 taskId, uint8 pipeId,
				const void* data, size_t length, void* response,
				uint32 responseCapacity,
				uint32* responseSize);
	status_t _IpcSendStreamCommand(uint16 commandId, uint8 taskId,
				uint8 pipeId, bool responseRequired);
	status_t _IpcPollService();
	status_t _IpcReceive(uint8 expectedMessage, uint8 expectedDriverId,
				void* response, uint32 responseCapacity,
				uint32* responseSize, bool* received);
	void _IpcAcknowledgeDsp(uint64 header);
	void _IpcClearHostDone();

	// Playback stream lifecycle
	status_t _PreparePlaybackHardware();
	status_t _ConfigurePlaybackRoute();
	status_t _FailPlaybackRoute(const char* step, status_t status);
	status_t _AllocateStream();
	status_t _FailStreamAllocation(status_t status);
	status_t _StartStream();
	status_t _StopStream();
	status_t _FreeStream();

	mutex					fLock;
	mutex					fCodecLock;
	mutex					fJackLock;
	mutex					fStreamLock;
	mutex					fIpcLock;
	bool					fLpePresent;
	bool					fCodecPresent;
	status_t				fLpeStatus;
	status_t				fCodecStatus;
	int32					fOpenCount;

	i2c_device_interface*	fI2c;
	i2c_device				fI2cCookie;
	device_node*			fCodecNode;
	uint8					fCodecRevision;
	uint8					fVolume;
	bool					fMuted;
	uint8					fHeadphoneVolume;
	bool					fHeadphoneMuted;
	bool					fHeadphonePresent;
	bool					fMicrophonePresent;
	gpio::module_info*		fGpio;
	gpio::Pin				fHeadphoneDetect;
	gpio::Pin				fMicrophoneDetect;

	area_id					fIramArea;
	area_id					fDramArea;
	area_id					fShimArea;
	area_id					fMailboxArea;
	area_id					fDdrArea;
	volatile uint8*			fIram;
	volatile uint8*			fDram;
	volatile uint8*			fShim;
	volatile uint8*			fMailbox;
	volatile uint8*			fDdr;
	size_t					fDdrSize;
	uint32					fIpcIrq;

	bool					fPmcReserved;
	pci_info				fPmcInfo;
	area_id					fPmcArea;
	volatile uint8*			fPmcRegisters;

	area_id					fPlaybackArea;
	void*					fPlaybackBuffer;
	phys_addr_t				fPlaybackPhysical;
	uint32					fPeriodFrames;
	int32					fPeriodCount;

	// Stream state
	enum StreamState {
		kStreamIdle,
		kStreamAllocated,
		kStreamRunning,
		kStreamStopping
	};
	StreamState				fStreamState;
	bool					fHardwareConfigured;
	bool					fRouteConfigured;
	status_t				fRouteFault;
	status_t				fAllocationFault;
	bool					fExchangeWanted;
	int32					fPeriodElapsedCount;
	int32					fPlaybackFault;
	uint64					fLastHardwareCounter;
};

extern Card* gCard;

} // namespace jr::byt_audio
