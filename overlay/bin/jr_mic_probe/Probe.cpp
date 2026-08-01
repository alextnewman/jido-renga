// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

#include <Application.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <OS.h>
#include <TimeSource.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


namespace {

constexpr uint32 kSampleRate = 48000;
constexpr uint16 kChannelCount = 2;
constexpr uint16 kBitsPerSample = 16;
constexpr uint32 kMinimumSeconds = 1;
constexpr uint32 kMaximumSeconds = 30;


#pragma pack(push, 1)
struct WaveHeader {
	char riff[4];
	uint32 size;
	char wave[4];
	char formatTag[4];
	uint32 formatSize;
	uint16 encoding;
	uint16 channels;
	uint32 sampleRate;
	uint32 bytesPerSecond;
	uint16 frameBytes;
	uint16 bitsPerSample;
	char dataTag[4];
	uint32 dataSize;
};
#pragma pack(pop)

static_assert(sizeof(WaveHeader) == 44);


struct CaptureState {
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	int fd = -1;
	status_t status = B_OK;
	uint64 bytes = 0;
	uint64 samples = 0;
	uint64 nonzeroSamples = 0;
	uint64 clippedSamples = 0;
	uint64 callbacks = 0;
	long double sumSquares = 0;
	int32 peak = 0;
	media_raw_audio_format format = {};
	bool haveFormat = false;
};


struct CaptureSnapshot {
	status_t status;
	uint64 bytes;
	uint64 samples;
	uint64 nonzeroSamples;
	uint64 clippedSamples;
	uint64 callbacks;
	long double sumSquares;
	int32 peak;
	media_raw_audio_format format;
	bool haveFormat;
};


CaptureSnapshot
SnapshotCapture(CaptureState& state)
{
	pthread_mutex_lock(&state.lock);
	const CaptureSnapshot snapshot = {
		state.status,
		state.bytes,
		state.samples,
		state.nonzeroSamples,
		state.clippedSamples,
		state.callbacks,
		state.sumSquares,
		state.peak,
		state.format,
		state.haveFormat
	};
	pthread_mutex_unlock(&state.lock);
	return snapshot;
}


struct StopContext {
	BMediaRecorder* recorder;
	uint32 seconds;
	status_t stopStatus = B_ERROR;
	status_t disconnectStatus = B_ERROR;
};


int32
StopCapture(void* cookie)
{
	StopContext* context = static_cast<StopContext*>(cookie);
	snooze(static_cast<bigtime_t>(context->seconds) * 1000000);
	context->stopStatus = context->recorder->Stop(true);
	context->disconnectStatus = context->recorder->Disconnect();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return B_OK;
}


WaveHeader
MakeWaveHeader(uint32 dataSize)
{
	WaveHeader header = {};
	memcpy(header.riff, "RIFF", 4);
	header.size = sizeof(WaveHeader) - 8 + dataSize;
	memcpy(header.wave, "WAVE", 4);
	memcpy(header.formatTag, "fmt ", 4);
	header.formatSize = 16;
	header.encoding = 1;
	header.channels = kChannelCount;
	header.sampleRate = kSampleRate;
	header.frameBytes = kChannelCount * sizeof(int16);
	header.bytesPerSecond = header.sampleRate * header.frameBytes;
	header.bitsPerSample = kBitsPerSample;
	memcpy(header.dataTag, "data", 4);
	header.dataSize = dataSize;
	return header;
}


bool
WriteAll(int fd, const void* data, size_t size)
{
	const uint8* bytes = static_cast<const uint8*>(data);
	size_t done = 0;
	while (done < size) {
		const ssize_t written = write(fd, bytes + done, size - done);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return false;
		done += static_cast<size_t>(written);
	}
	return true;
}


void
Record(void* cookie, bigtime_t, void* data, size_t size,
	const media_format& format)
{
	CaptureState* state = static_cast<CaptureState*>(cookie);
	pthread_mutex_lock(&state->lock);
	if (state->status != B_OK) {
		pthread_mutex_unlock(&state->lock);
		return;
	}
	const media_raw_audio_format& raw = format.u.raw_audio;
	if (format.type != B_MEDIA_RAW_AUDIO
		|| raw.format != media_raw_audio_format::B_AUDIO_SHORT
		|| raw.channel_count != kChannelCount
		|| static_cast<uint32>(raw.frame_rate) != kSampleRate
		|| raw.byte_order != B_MEDIA_LITTLE_ENDIAN
		|| size % sizeof(int16) != 0) {
		state->status = B_MEDIA_BAD_FORMAT;
		pthread_mutex_unlock(&state->lock);
		return;
	}
	if (!WriteAll(state->fd, data, size)) {
		state->status = B_IO_ERROR;
		pthread_mutex_unlock(&state->lock);
		return;
	}
	if (!state->haveFormat) {
		state->format = raw;
		state->haveFormat = true;
	}
	const int16* samples = static_cast<const int16*>(data);
	const size_t sampleCount = size / sizeof(int16);
	for (size_t index = 0; index < sampleCount; index++) {
		const int32 value = samples[index];
		const int32 magnitude = value < 0 ? -value : value;
		if (magnitude > state->peak)
			state->peak = magnitude;
		if (value != 0)
			state->nonzeroSamples++;
		if (magnitude >= 32760)
			state->clippedSamples++;
		state->sumSquares += static_cast<long double>(value) * value;
	}
	state->bytes += size;
	state->samples += sampleCount;
	state->callbacks++;
	pthread_mutex_unlock(&state->lock);
}


bool
ParseSeconds(const char* text, uint32& seconds)
{
	char* end = nullptr;
	errno = 0;
	const unsigned long value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0'
		|| value < kMinimumSeconds || value > kMaximumSeconds) {
		return false;
	}
	seconds = static_cast<uint32>(value);
	return true;
}


void
Usage()
{
	fprintf(stderr, "usage: jr_mic_probe [OUTPUT.wav] [SECONDS]\n");
}

} // namespace


int
main(int argc, char** argv)
{
	if (argc > 3) {
		Usage();
		return 2;
	}
	const char* output = argc >= 2 ? argv[1] : "/boot/home/jr_mic.wav";
	uint32 seconds = 5;
	if (argc >= 3 && !ParseSeconds(argv[2], seconds)) {
		Usage();
		return 2;
	}

	BApplication application("application/x-vnd.JidoRenga-jr-mic-probe");
	status_t status = B_OK;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	if (roster == nullptr || status != B_OK) {
		fprintf(stderr, "jr_mic: media roster unavailable: %s\n",
			strerror(status));
		return 1;
	}

	media_node input;
	status = roster->GetAudioInput(&input);
	if (status != B_OK) {
		fprintf(stderr, "jr_mic: default audio input unavailable: %s\n",
			strerror(status));
		return 1;
	}
	live_node_info inputInfo = {};
	if (roster->GetLiveNodeInfo(input, &inputInfo) == B_OK)
		printf("jr_mic: audio input node %s\n", inputInfo.name);

	CaptureState state;
	state.fd = open(output, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (state.fd < 0) {
		fprintf(stderr, "jr_mic: cannot create %s: %s\n", output,
			strerror(errno));
		return 1;
	}
	const WaveHeader emptyHeader = MakeWaveHeader(0);
	if (!WriteAll(state.fd, &emptyHeader, sizeof(emptyHeader))) {
		fprintf(stderr, "jr_mic: cannot write WAV header\n");
		close(state.fd);
		return 1;
	}

	BMediaRecorder recorder("Jido Renga microphone probe", B_MEDIA_RAW_AUDIO);
	if (recorder.InitCheck() != B_OK) {
		fprintf(stderr, "jr_mic: recorder initialization failed: %s\n",
			strerror(recorder.InitCheck()));
		close(state.fd);
		return 1;
	}
	media_format accepted = {};
	accepted.type = B_MEDIA_RAW_AUDIO;
	accepted.u.raw_audio.frame_rate = kSampleRate;
	accepted.u.raw_audio.channel_count = kChannelCount;
	accepted.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	accepted.u.raw_audio.byte_order = B_MEDIA_LITTLE_ENDIAN;
	accepted.u.raw_audio.buffer_size = 0;
	recorder.SetAcceptedFormat(accepted);
	status = recorder.SetHooks(Record, nullptr, &state);
	if (status == B_OK)
		status = recorder.Connect(input, nullptr, &accepted);
	if (status == B_OK)
		status = recorder.Start(true);
	if (status == B_OK) {
		const status_t producerStatus = roster->StartNode(input,
			BTimeSource::RealTime());
		if (producerStatus != B_OK && producerStatus != B_ALREADY_RUNNING) {
			status = producerStatus;
		} else {
			printf("jr_mic: producer node explicitly started\n");
		}
	}
	if (status != B_OK) {
		fprintf(stderr, "jr_mic: capture start failed: %s\n",
			strerror(status));
		recorder.Disconnect();
		close(state.fd);
		return 1;
	}
	printf("jr_mic: recording %u second(s) to %s; speak near the "
		"display\n", seconds, output);
	fflush(stdout);
	StopContext stopContext = {&recorder, seconds};
	thread_id stopThread = spawn_thread(StopCapture, "jr_mic stop timer",
		B_NORMAL_PRIORITY, &stopContext);
	if (stopThread < B_OK || resume_thread(stopThread) != B_OK) {
		fprintf(stderr, "jr_mic: cannot start capture timer\n");
		recorder.Stop(true);
		recorder.Disconnect();
		close(state.fd);
		return 1;
	}
	application.Run();
	status_t threadStatus = B_OK;
	wait_for_thread(stopThread, &threadStatus);
	const status_t stopStatus = stopContext.stopStatus;
	const status_t disconnectStatus = stopContext.disconnectStatus;

	const CaptureSnapshot captured = SnapshotCapture(state);
	const status_t captureStatus = captured.status;
	const uint64 bytes = captured.bytes;
	const uint64 samples = captured.samples;

	const uint32 dataSize = bytes <= UINT32_MAX
		? static_cast<uint32>(bytes) : UINT32_MAX;
	const WaveHeader header = MakeWaveHeader(dataSize);
	if (lseek(state.fd, 0, SEEK_SET) < 0
		|| !WriteAll(state.fd, &header, sizeof(header))) {
		status = B_IO_ERROR;
	}
	if (close(state.fd) != 0)
		status = B_IO_ERROR;

	const double rms = samples != 0
		? sqrt(static_cast<double>(captured.sumSquares / samples)) : 0;
	const double nonzeroPercent = samples != 0
		? static_cast<double>(captured.nonzeroSamples) * 100.0 / samples : 0;
	const double clippedPercent = samples != 0
		? static_cast<double>(captured.clippedSamples) * 100.0 / samples : 0;
	if (captured.haveFormat) {
		printf("jr_mic: format=%.0fHz channels=%" B_PRIu32
			" sample_format=0x%08" B_PRIx32 " buffer=%" B_PRIuSIZE
			"\n", captured.format.frame_rate, captured.format.channel_count,
			captured.format.format, captured.format.buffer_size);
	}
	printf("jr_mic: callbacks=%" B_PRIu64 " bytes=%" B_PRIu64
		" frames=%" B_PRIu64 " peak=%" B_PRId32
		" rms=%.2f nonzero=%.3f%% clipped=%.3f%%\n",
		captured.callbacks, bytes, samples / kChannelCount, captured.peak,
		rms, nonzeroPercent, clippedPercent);

	if (status != B_OK || stopStatus != B_OK || disconnectStatus != B_OK
		|| captureStatus != B_OK) {
		fprintf(stderr, "jr_mic: capture failed: record=%s stop=%s "
			"disconnect=%s file=%s\n", strerror(captureStatus),
			strerror(stopStatus), strerror(disconnectStatus),
			strerror(status));
		return 1;
	}
	if (bytes == 0 || samples == 0 || captured.peak == 0) {
		fprintf(stderr, "jr_mic: no nonzero microphone samples received\n");
		return 1;
	}
	if (clippedPercent > 90.0) {
		fprintf(stderr, "jr_mic: microphone remains pinned at full scale\n");
		return 1;
	}
	printf("jr_mic: PASS\n");
	return 0;
}
