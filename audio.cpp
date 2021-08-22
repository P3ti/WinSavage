#include <stdint.h>
#include <stdexcept>
#include <iomanip>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

#define NOMINMAX
#include <windows.h>
#include <dsound.h>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x) \
   if(x != NULL)        \
   {                    \
      x->Release();     \
      x = NULL;         \
   }
#endif

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

#include "patchutil.h"

#define WAV_FOURCC_RIFF 'FFIR'
#define WAV_FOURCC_WAVE 'EVAW'
#define WAV_FOURCC_FMT ' tmf'
#define WAV_FOURCC_DATA 'atad'

#pragma pack(push, 1)
struct WavChunkHeader
{
	uint32_t chunkId;
	uint32_t chunkSize;
};

struct WavMasterChunk : public WavChunkHeader
{
	uint32_t format;
};

struct WavFormatChunk : public WavChunkHeader
{
	uint16_t audioFormat;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t bytesPerSecond;
	uint16_t blockAlign;
	uint16_t bitsPerSample;
};
#pragma pack(pop)

static_assert(sizeof(WavChunkHeader) == 8, "Wrong size: WavChunkHeader");
static_assert(sizeof(WavMasterChunk) == sizeof(WavChunkHeader) + 4, "Wrong size: WavMasterChunk");
static_assert(sizeof(WavFormatChunk) == sizeof(WavChunkHeader) + 16, "Wrong size: WavFormatChunk");

int Displayf(const char* format, ...);
void FatalError(const char* format, ...);

void DecodeDviImaAdpcmBlock(const uint8_t*& blockPtr, bool stereo, uint16_t nibblesPerBlock, int16_t*& samplePtr);

IDirectSound8* DSound = NULL;
IDirectSoundBuffer* DSoundPrimaryBuffer = NULL;

extern HWND GameWindow;

void StartupAudioSystem()
{
	if(GameWindow == NULL)
		FatalError("Need game window for initializing DirectSound");

	if(FAILED(DirectSoundCreate8(NULL, &DSound, NULL)))
		FatalError("Failed to create DirectSound device");

	if(FAILED(DSound->SetCooperativeLevel(GameWindow, DSSCL_PRIORITY)))
		FatalError("Could not set DirectSound cooperative level");

	DSBUFFERDESC primaryBufferDesc;
	primaryBufferDesc.dwSize = sizeof(DSBUFFERDESC);
	primaryBufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;
	primaryBufferDesc.dwBufferBytes = 0;
	primaryBufferDesc.dwReserved = 0;
	primaryBufferDesc.lpwfxFormat = NULL;
	primaryBufferDesc.guid3DAlgorithm = GUID_NULL;
	if(FAILED(DSound->CreateSoundBuffer(&primaryBufferDesc, &DSoundPrimaryBuffer, NULL)))
		FatalError("Failed to create primary DirectSound buffer");

	WAVEFORMATEX primaryBufferFormat;
	primaryBufferFormat.wFormatTag = WAVE_FORMAT_PCM;
	primaryBufferFormat.nSamplesPerSec = 44100;
	primaryBufferFormat.wBitsPerSample = 16;
	primaryBufferFormat.nChannels = 2;
	primaryBufferFormat.nBlockAlign = (primaryBufferFormat.wBitsPerSample / 8) * primaryBufferFormat.nChannels;
	primaryBufferFormat.nAvgBytesPerSec = primaryBufferFormat.nSamplesPerSec * primaryBufferFormat.nBlockAlign;
	primaryBufferFormat.cbSize = 0;
	if(FAILED(DSoundPrimaryBuffer->SetFormat(&primaryBufferFormat)))
		FatalError("Failed to set format of primary DirectSound buffer");
}

void ShutdownAudioSystem()
{
	SAFE_RELEASE(DSoundPrimaryBuffer);
	SAFE_RELEASE(DSound);
}

IDirectSoundBuffer* CreateDSoundBuffer(const WavFormatChunk& format, DWORD flags, uint32_t size)
{
	if(format.audioFormat != WAVE_FORMAT_PCM)
		return nullptr;

	WAVEFORMATEX wfxFormat;
	wfxFormat.wFormatTag = WAVE_FORMAT_PCM;
	wfxFormat.nSamplesPerSec = format.sampleRate;
	wfxFormat.wBitsPerSample = format.bitsPerSample;
	wfxFormat.nChannels = format.numChannels;
	wfxFormat.nBlockAlign = (wfxFormat.wBitsPerSample / 8) * wfxFormat.nChannels;
	wfxFormat.nAvgBytesPerSec = wfxFormat.nSamplesPerSec * wfxFormat.nBlockAlign;
	wfxFormat.cbSize = 0;

	DSBUFFERDESC bufferDesc;
	bufferDesc.dwSize = sizeof(DSBUFFERDESC);
	bufferDesc.dwFlags = flags;
	bufferDesc.dwBufferBytes = size;
	bufferDesc.dwReserved = 0;
	bufferDesc.lpwfxFormat = &wfxFormat;
	bufferDesc.guid3DAlgorithm = GUID_NULL;

	IDirectSoundBuffer* tempBuffer;
	if(FAILED(DSound->CreateSoundBuffer(&bufferDesc, &tempBuffer, NULL)))
		return nullptr;

	IDirectSoundBuffer8* buffer;
	HRESULT result = tempBuffer->QueryInterface(IID_IDirectSoundBuffer8, (void**)&buffer);
	tempBuffer->Release();
	return SUCCEEDED(result) ? buffer : nullptr;
}

struct DSoundBufferWriter
{
public:
	DSoundBufferWriter(IDirectSoundBuffer* buffer, uint32_t offset, uint32_t size)
		: buffer(buffer)
		, lockResult(buffer->Lock(offset, size, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0)) { }

	~DSoundBufferWriter()
	{
		if(SUCCEEDED(lockResult))
			buffer->Unlock(audioPtr1, audioBytes1, audioPtr2, audioBytes2);
	}

	operator bool() const
	{
		return SUCCEEDED(lockResult);
	}

	size_t WriteSilence(uint16_t bitsPerSample)
	{
		if(FAILED(lockResult))
			return 0;

		int v = bitsPerSample == 8 ? 0x80 : 0x00;
		memset(audioPtr1, v, audioBytes1);

		if(audioPtr2)
			memset(audioPtr2, v, audioBytes2);

		return audioBytes1 + audioBytes2;
	}

	size_t WriteFromMemory(const int16_t* data)
	{
		if(FAILED(lockResult))
			return 0;

		memcpy(audioPtr1, data, audioBytes1);

		if(audioPtr2)
			memcpy(audioPtr2, data + audioBytes1, audioBytes2);

		return audioBytes1 + audioBytes2;
	}

	size_t WriteFromFile(std::ifstream& file)
	{
		if(FAILED(lockResult))
			return 0;

		if(!file.read(reinterpret_cast<char*>(audioPtr1), audioBytes1))
			return 0;

		if(audioPtr2 && !file.read(reinterpret_cast<char*>(audioPtr2), audioBytes2))
			return 0;

		return audioBytes1 + audioBytes2;
	}

private:
	IDirectSoundBuffer* buffer;
	LPVOID audioPtr1 = NULL, audioPtr2 = NULL;
	DWORD audioBytes1 = 0, audioBytes2 = 0;
	HRESULT lockResult;
};

struct AudioSample
{
	IDirectSoundBuffer* buffer = NULL;
	int32_t pan = 64;
	int32_t volume = 127;

	AudioSample() = default;
	AudioSample(const AudioSample&) = delete;
	AudioSample& operator=(const AudioSample&) = delete;
	~AudioSample() { SAFE_RELEASE(buffer); }
};

AudioSample* __cdecl AllocateAudioSampleHandle(uintptr_t /*DIB*/)
{
	return new AudioSample;
}

uint32_t __cdecl GetAudioSamplePlaybackRate(AudioSample* sample)
{
	if(!sample->buffer)
		return 0;

	DWORD playbackRate;
	if(FAILED(sample->buffer->GetFrequency(&playbackRate)))
		return 0;

	return playbackRate;
}

uint32_t DsbStatusToMss(IDirectSoundBuffer* buffer)
{
	DWORD status;
	if(FAILED(buffer->GetStatus(&status)))
		return 1;

	if((status & DSBSTATUS_PLAYING) == DSBSTATUS_PLAYING)
		return 4;

	DWORD currentPlayCursor;
	if(FAILED(buffer->GetCurrentPosition(&currentPlayCursor, NULL)))
		return 2;

	return currentPlayCursor > 0 ? 8 : 2;
}

uint32_t __cdecl GetAudioSampleStatus(AudioSample* sample)
{
	return sample->buffer ? DsbStatusToMss(sample->buffer) : 1;
}

LONG MssPanToDsb(int32_t pan)
{
	return (LONG)roundf(pan * (DSBPAN_RIGHT - DSBPAN_LEFT) / 127.0f + DSBPAN_LEFT);
}

void __cdecl SetAudioSamplePan(AudioSample* sample, int32_t pan)
{
	if(sample->buffer)
		sample->buffer->SetPan(MssPanToDsb(pan));

	sample->pan = pan;
}

LONG MssVolumeToDsb(int32_t volume)
{
	double attenuation = 1.0 / 1024.0 + volume / 127.0 * 1023.0 / 1024;
	double db = 10.0 * log10(attenuation) / log10(2);
	return (LONG)(db * 100.0);
}

void __cdecl SetAudioSampleVolume(AudioSample* sample, int32_t volume)
{
	if(sample->buffer)
		sample->buffer->SetVolume(MssVolumeToDsb(volume));

	sample->volume = volume;
}

void __cdecl SetAudioSamplePlaybackRate(AudioSample* sample, uint32_t playbackRate)
{
	if(sample->buffer)
		sample->buffer->SetFrequency(playbackRate);
}

void __cdecl EndAudioSample(AudioSample* sample)
{
	if(sample->buffer)
	{
		sample->buffer->Stop();
		SAFE_RELEASE(sample->buffer);
	}
}

void __cdecl ResumeAudioSample(AudioSample* sample)
{
	if(sample->buffer)
	{
		sample->buffer->SetPan(MssPanToDsb(sample->pan));
		sample->buffer->SetVolume(MssVolumeToDsb(sample->volume));
		sample->buffer->Play(0, 0, 0);
	}
}

void __cdecl StopAudioSample(AudioSample* sample)
{
	if(sample->buffer)
		sample->buffer->Stop();
}

void __cdecl StartAudioSample(AudioSample* sample)
{
	if(sample->buffer)
	{
		sample->buffer->SetCurrentPosition(0);
		ResumeAudioSample(sample);
	}
}

void __cdecl SetAudioSampleFile(AudioSample* sample, const void* wavData, uintptr_t)
{
	static constexpr DWORD SampleBufferFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;

	const WavMasterChunk* masterChunk = reinterpret_cast<const WavMasterChunk*>(wavData);
	if(masterChunk->chunkId != WAV_FOURCC_RIFF || masterChunk->format != WAV_FOURCC_WAVE)
		return;

	size_t wavSize = sizeof(WavChunkHeader) + masterChunk->chunkSize;
	if(wavSize < sizeof(WavMasterChunk))
		return;

	WavFormatChunk format = { };
	const uint8_t* data = nullptr;
	size_t dataChunkSize = 0;

	size_t chunkOffset = sizeof(WavMasterChunk);
	while(wavSize >= chunkOffset + sizeof(WavChunkHeader))
	{
		const void* chunkPtr = reinterpret_cast<const char*>(wavData) + chunkOffset;

		WavChunkHeader chunkHeader;
		memcpy(&chunkHeader, chunkPtr, sizeof(WavChunkHeader));

		if(chunkHeader.chunkId == WAV_FOURCC_FMT)
		{
			format = *reinterpret_cast<const WavFormatChunk*>(chunkPtr);
		}
		else if(chunkHeader.chunkId == WAV_FOURCC_DATA)
		{
			data = reinterpret_cast<const uint8_t*>(chunkPtr) + sizeof(WavChunkHeader);
			dataChunkSize = reinterpret_cast<const WavChunkHeader*>(chunkPtr)->chunkSize;
		}

		chunkOffset += sizeof(WavChunkHeader) + chunkHeader.chunkSize;
	}

	if(format.chunkId != WAV_FOURCC_FMT || !data)
		return;

	const int16_t* pcmData;
	size_t pcmDataSize;
	std::unique_ptr<int16_t[]> decodeBuf;

	if(format.audioFormat == 1) // PCM
	{
		pcmData = reinterpret_cast<const int16_t*>(data);
		pcmDataSize = dataChunkSize;
	}
	else if(format.audioFormat == 17) // DVI IMA ADPCM
	{
		bool stereo = format.numChannels == 2;
		if(format.numChannels != 1 && !stereo)
		{
			Displayf("Wave data encoded with DVI IMA ADPCM should have 1 or 2 channels instead of %hu", format.numChannels);
			return;
		}

		if(format.bitsPerSample != 4)
		{
			Displayf("Wave data encoded with DVI IMA ADPCM does not have 4-bit samples: %hu", format.bitsPerSample);
			return;
		}

		uint16_t nibblesPerBlock = 2 * (format.blockAlign - 4 * format.numChannels);

		pcmDataSize = (dataChunkSize / format.blockAlign) * nibblesPerBlock;
		decodeBuf = std::make_unique<int16_t[]>(pcmDataSize);
		pcmDataSize *= sizeof(int16_t);
		pcmData = decodeBuf.get();

		const uint8_t* dataPtr = data;
		const uint8_t* endOfDataChunk = dataPtr + dataChunkSize;
		int16_t* decodePtr = decodeBuf.get();
		while(dataPtr < endOfDataChunk)
			DecodeDviImaAdpcmBlock(dataPtr, stereo, nibblesPerBlock, decodePtr);

		format.audioFormat = 1;
		format.bitsPerSample = 16;
		format.blockAlign = (format.bitsPerSample / 8) * format.numChannels;
		format.bytesPerSecond = format.sampleRate * format.blockAlign;
	}
	else
	{
		Displayf("Sample has unsupported wave audio format: %hu", format.audioFormat);
		return;
	}

	SAFE_RELEASE(sample->buffer);
	sample->buffer = CreateDSoundBuffer(format, SampleBufferFlags, pcmDataSize);
	if(sample->buffer)
	{
		DSoundBufferWriter writer(sample->buffer, 0, pcmDataSize);
		writer.WriteFromMemory(pcmData);
	}
}

void __cdecl InitAudioSample(AudioSample* sample)
{
	sample->~AudioSample();
	new (sample) AudioSample;
}

void __cdecl ReleaseAudioSampleHandle(AudioSample* sample)
{
	delete sample;
}

#pragma pack(push, 1)
struct AudioStreamLoopInfo
{
	uint32_t startOffset = 0;
	uint32_t endOffset = 0;
	int32_t totalIterations = 1;
	int32_t numIterations = 0;
	int16_t nextLoopIndex = -1;
};
static_assert(sizeof(AudioStreamLoopInfo) == 18, "Wrong size: AudioStreamLoopInfo");
#pragma pack(pop)

struct AudioStream
{
	static constexpr DWORD ServiceBufferFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;
	static constexpr uint32_t ServiceBufferLengthMs = 2000;
	static constexpr uint32_t ServiceIntervalMs = 250;

	std::ifstream file;
	WavFormatChunk format = { };
	size_t streamStartOffset = 0;
	size_t streamSize = 0;
	size_t streamOffset = 0;

	IDirectSoundBuffer* serviceBuffer = NULL;
	uint32_t serviceBufferSize = 0;
	uint32_t serviceBufferOffset = 0;
	uint32_t serviceTimerId = 0;
	std::atomic<bool> inService;

	int32_t volume = 127;

	std::vector<AudioStreamLoopInfo> loops;
	int32_t currentLoopIndex = 0;

private:
	AudioStream() = default;

public:
	static AudioStream* Open(const char* wavFilePath);
	~AudioStream();

	AudioStream(const AudioStream&) = delete;
	AudioStream& operator=(const AudioStream&) = delete;

	void Start(int32_t loopIndex);
	void Pause(bool onOff);

private:
	static void CALLBACK ServiceTimerCallback(UINT timerId, UINT msg, DWORD userPtr, DWORD dw1, DWORD dw2);

	uint32_t GetWriteSize();
	uint32_t WriteData(uint32_t size);
	uint32_t WriteSilence(uint32_t size);

	void Service();
};

AudioStream* AudioStream::Open(const char* wavFilePath)
{
	std::unique_ptr<AudioStream> stream(new AudioStream);
	stream->file.open(wavFilePath, std::ios::in | std::ios::binary | std::ios::ate);
	if(!stream->file.is_open())
		return nullptr;

	size_t fileSize = static_cast<size_t>(stream->file.tellg());
	stream->file.seekg(0, std::ios::beg);

	WavMasterChunk masterChunk;
	stream->file.read(reinterpret_cast<char*>(&masterChunk), sizeof(WavMasterChunk));
	if(masterChunk.chunkId != WAV_FOURCC_RIFF || masterChunk.format != WAV_FOURCC_WAVE ||
		masterChunk.chunkSize < sizeof(WavMasterChunk) || masterChunk.chunkSize > fileSize)
	{
		return nullptr;
	}

	size_t chunkOffset = sizeof(WavMasterChunk);
	while(masterChunk.chunkSize >= chunkOffset + sizeof(WavChunkHeader))
	{
		WavChunkHeader chunkHeader;
		if(!stream->file.read(reinterpret_cast<char*>(&chunkHeader), sizeof(WavChunkHeader)))
			return nullptr;

		if(chunkHeader.chunkId == WAV_FOURCC_FMT)
		{
			stream->format.chunkId = chunkHeader.chunkId;
			stream->format.chunkSize = chunkHeader.chunkSize;

			char* formatDataPtr = reinterpret_cast<char*>(&stream->format) + sizeof(WavChunkHeader);
			if(!stream->file.read(formatDataPtr, sizeof(WavFormatChunk) - sizeof(WavChunkHeader)))
				return nullptr;
		}
		else if(chunkHeader.chunkId == WAV_FOURCC_DATA)
		{
			stream->streamStartOffset = chunkOffset + sizeof(WavChunkHeader);
			stream->streamSize = chunkHeader.chunkSize;
		}

		chunkOffset += sizeof(WavChunkHeader) + chunkHeader.chunkSize;
		stream->file.seekg(chunkOffset, std::ios::beg);
	}

	if(stream->format.chunkId != WAV_FOURCC_FMT ||
		stream->format.audioFormat != 1 || // PCM
		!stream->streamStartOffset || !stream->streamSize || stream->streamSize > fileSize)
	{
		return nullptr;
	}

	stream->serviceBufferSize = std::min((stream->format.bytesPerSecond * ServiceBufferLengthMs) / 1000, stream->streamSize);
	stream->serviceBuffer = CreateDSoundBuffer(stream->format, ServiceBufferFlags, stream->serviceBufferSize);
	return stream->serviceBuffer ? stream.release() : nullptr;
}

AudioStream::~AudioStream()
{
	if(serviceTimerId)
		Pause(true);

	SAFE_RELEASE(serviceBuffer);
}

void AudioStream::Start(int32_t loopIndex)
{
	if(loopIndex < 0 || static_cast<size_t>(loopIndex) >= loops.size())
		return;

	currentLoopIndex = loopIndex;

	if(serviceTimerId)
		Pause(true);

	AudioStreamLoopInfo& loop = loops[currentLoopIndex];
	loop.numIterations = 0;

	file.seekg(static_cast<std::streamoff>(streamStartOffset) + loop.startOffset, std::ios::beg);
	streamOffset = loop.startOffset + WriteData(serviceBufferSize);

	Pause(false);
}

void AudioStream::Pause(bool onOff)
{
	if(!streamOffset)
		return;

	if(onOff == 0)
	{
		if(serviceTimerId)
			return;

		inService.store(false);

		serviceBuffer->SetVolume(MssVolumeToDsb(volume));
		serviceBuffer->Play(0, 0, DSBPLAY_LOOPING);

		serviceTimerId = timeSetEvent(ServiceIntervalMs, ServiceIntervalMs, ServiceTimerCallback, (DWORD)this, TIME_PERIODIC);
	}
	else
	{
		if(!serviceTimerId)
			return;

		serviceBuffer->Stop();

		timeKillEvent(serviceTimerId);
		serviceTimerId = 0;

		while(inService.load());
	}
}

void CALLBACK AudioStream::ServiceTimerCallback(UINT timerId, UINT msg, DWORD userPtr, DWORD dw1, DWORD dw2)
{
	AudioStream* stream = (AudioStream*)userPtr;
	if(!stream->inService.exchange(true))
	{
		stream->Service();
		stream->inService.store(false);
	}
}

uint32_t AudioStream::GetWriteSize()
{
	DWORD playCursor;
	if(FAILED(serviceBuffer->GetCurrentPosition(&playCursor, NULL)))
		return 0;

	if(serviceBufferOffset <= playCursor)
		return playCursor - serviceBufferOffset;
	else
		return serviceBufferSize - serviceBufferOffset + playCursor;
}

uint32_t AudioStream::WriteData(uint32_t size)
{
	if(size > serviceBufferSize)
		return false;

	DSoundBufferWriter writer(serviceBuffer, serviceBufferOffset, size);
	size_t bytesWritten = writer.WriteFromFile(file);
	serviceBufferOffset = (serviceBufferOffset + bytesWritten) % serviceBufferSize;
	return bytesWritten;
}

uint32_t AudioStream::WriteSilence(uint32_t size)
{
	if(size > serviceBufferSize)
		return false;

	DSoundBufferWriter writer(serviceBuffer, serviceBufferOffset, size);
	size_t bytesWritten = writer.WriteSilence(format.bitsPerSample);
	serviceBufferOffset = (serviceBufferOffset + bytesWritten) % serviceBufferSize;
	return bytesWritten;
}

void AudioStream::Service()
{
	uint32_t writeSize = GetWriteSize();
	uint32_t remainingBytes = writeSize;

	AudioStreamLoopInfo& currentLoop = loops[currentLoopIndex];
	if(streamOffset < currentLoop.endOffset)
	{
		size_t remainingFromCurrentLoop = currentLoop.endOffset - streamOffset;
		if(remainingFromCurrentLoop >= writeSize)
		{
			streamOffset += WriteData(writeSize);
			return;
		}

		size_t bytesWritten = WriteData(remainingFromCurrentLoop);
		streamOffset += bytesWritten;
		remainingBytes -= bytesWritten;
	}

	if(streamOffset != currentLoop.endOffset)
	{
		WriteSilence(remainingBytes);
		return;
	}

	if(currentLoop.totalIterations && currentLoop.nextLoopIndex != -1)
	{
		if(currentLoop.numIterations < currentLoop.totalIterations)
			++currentLoop.numIterations;

		if(currentLoop.numIterations >= currentLoop.totalIterations)
		{
			if(currentLoop.nextLoopIndex >= 0 && static_cast<size_t>(currentLoop.nextLoopIndex) < loops.size())
				currentLoopIndex = currentLoop.nextLoopIndex;
			else
				currentLoopIndex = 0;
		}
	}

	streamOffset = currentLoop.startOffset;
	file.seekg(static_cast<std::streamoff>(streamStartOffset) + streamOffset, std::ios::beg);

	size_t bytesWritten = WriteData(remainingBytes);
	streamOffset += bytesWritten;
	remainingBytes -= bytesWritten;

	if(remainingBytes)
		WriteSilence(remainingBytes);
}

uint32_t __cdecl GetAudioStreamStatus(AudioStream* stream)
{
	return DsbStatusToMss(stream->serviceBuffer);
}

void __cdecl SetAudioStreamVolume(AudioStream* stream, int32_t volume)
{
	stream->serviceBuffer->SetVolume(MssVolumeToDsb(volume));
	stream->volume = volume;
}

void __cdecl PauseAudioStream(AudioStream* stream, int32_t onOff)
{
	stream->Pause(onOff);
}

void __cdecl CloseAudioStream(AudioStream* stream)
{
	delete stream;
}

int32_t __cdecl SetAudioStreamLoopInfo(AudioStream* stream, AudioStreamLoopInfo* info, uint32_t index)
{
	if(index >= stream->loops.size())
		return 0;

	if(!info->endOffset)
		info->endOffset = stream->streamSize;

	if(info->endOffset > stream->streamSize)
		info->endOffset = stream->streamSize;

	if(info->startOffset >= stream->streamSize)
		info->startOffset = 0;

	stream->loops[index] = *info;
	return 1;
}

void __cdecl StartAudioStream(AudioStream* stream, int32_t loopIndex)
{
	stream->Start(loopIndex);
}

int32_t __cdecl SetAudioStreamNumLoopIndices(AudioStream* stream, uint32_t numLoops)
{
	std::fill(stream->loops.begin(), stream->loops.end(), AudioStreamLoopInfo { });
	stream->loops.resize(numLoops);
	return 0;
}

void* __cdecl OpenAudioStream(uintptr_t /*DIB*/, const char* wavFilePath, uintptr_t)
{
	return AudioStream::Open(wavFilePath);
}

void PatchAudioSystem(uintptr_t SQ)
{
	//Patch(SQ + 0x2B9020, { 0 }); // no audio

	const uint8_t TRAP = 0xCC;
	Patch(SQ + 0x52A80, { TRAP }); // AIL_install_MDI_driver_file
	Patch(SQ + 0x52B50, { 0xC3 }); // AIL_install_MDI_INI
	Patch(SQ + 0x52C20, { TRAP }); // AIL_install_DIG_driver_file
	Patch(SQ + 0x52CF0, { 0xC3 }); // AIL_install_DIG_INI
	Patch(SQ + 0x52DC0, { TRAP }); // AIL_uninstall_driver
	Patch(SQ + 0x52E40, { TRAP }); // AIL_install_driver
	Patch(SQ + 0x52F10, { TRAP }); // AIL_get_IO_environment
	Patch(SQ + 0x52FE0, { TRAP }); // AIL_read_INI
	Patch(SQ + 0x53290, { TRAP }); // AIL_call_driver
	Patch(SQ + 0x53390, { TRAP }); // AIL_restore_USE16_ISR
	Patch(SQ + 0x53410, { TRAP }); // AIL_set_real_vect
	Patch(SQ + 0x534A0, { TRAP }); // AIL_get_real_vect
	Patch(SQ + 0x53570, { 0xC3 }); // AIL_set_driver_directory
	Patch(SQ + 0x53640, { TRAP }); // AIL_DLS_extract_image
	Patch(SQ + 0x53740, { TRAP }); // AIL_find_DLS_in_XMI
	Patch(SQ + 0x53830, { TRAP }); // AIL_file_type
	Patch(SQ + 0x53900, { TRAP }); // AIL_WAV_info
	Patch(SQ + 0x539D0, { TRAP }); // AIL_DLS_unload
	Patch(SQ + 0x53A60, { TRAP }); // AIL_DLS_load_memory
	Patch(SQ + 0x53B30, { TRAP }); // AIL_DLS_load_file
	Patch(SQ + 0x53C00, { TRAP }); // AIL_set_stream_position
	Hook(SQ + 0x53C90, 0xE9, GetAudioStreamStatus); // AIL_stream_status
	Patch(SQ + 0x53D60, { TRAP }); // AIL_set_stream_loop_count
	Hook(SQ + 0x53DF0, 0xE9, SetAudioStreamVolume); // AIL_set_stream_volume
	Hook(SQ + 0x53E80, 0xE9, PauseAudioStream); // AIL_pause_stream
	Patch(SQ + 0x53F10, { TRAP }); // AIL_start_stream
	Patch(SQ + 0x53F90, { 0xC3 }); // AIL_service_stream
	Hook(SQ + 0x54060, 0xE9, CloseAudioStream); // AIL_close_stream
	Patch(SQ + 0x540E0, { TRAP }); // ANGEL_current_loop_index
	Patch(SQ + 0x541A0, { TRAP }); // ANGEL_loop_info
	Hook(SQ + 0x54260, 0xE9, SetAudioStreamLoopInfo); // ANGEL_set_loop_info
	Hook(SQ + 0x54330, 0xE9, StartAudioStream); // ANGEL_start_stream
	Hook(SQ + 0x54410, 0xE9, SetAudioStreamNumLoopIndices); // ANGEL_set_num_loop_indices
	Hook(SQ + 0x544E0, 0xE9, OpenAudioStream); // AIL_open_stream
	Patch(SQ + 0x545B0, { TRAP }); // AIL_file_write
	Patch(SQ + 0x54680, { TRAP }); // AIL_file_read
	Patch(SQ + 0x54750, { TRAP }); // AIL_map_sequence_channel
	Patch(SQ + 0x547F0, { TRAP }); // AIL_release_channel
	Patch(SQ + 0x54880, { TRAP }); // AIL_lock_channel
	Patch(SQ + 0x54950, { TRAP }); // AIL_branch_index
	Patch(SQ + 0x549E0, { TRAP }); // AIL_sequence_position
	Patch(SQ + 0x54A80, { TRAP }); // AIL_sequence_status
	Patch(SQ + 0x54B50, { TRAP }); // AIL_set_sequence_loop_count
	Patch(SQ + 0x54BE0, { TRAP }); // AIL_set_sequence_volume
	Patch(SQ + 0x54C80, { TRAP }); // AIL_set_sequence_tempo
	Patch(SQ + 0x54D20, { TRAP }); // AIL_end_sequence
	Patch(SQ + 0x54DA0, { TRAP }); // AIL_resume_sequence
	Patch(SQ + 0x54E20, { TRAP }); // AIL_stop_sequence
	Patch(SQ + 0x54EA0, { TRAP }); // AIL_start_sequence
	Patch(SQ + 0x54F20, { TRAP }); // AIL_init_sequence
	Patch(SQ + 0x54FF0, { 0x31, 0xC0, 0xC3 }); // AIL_allocate_sequence_handle
	Patch(SQ + 0x550C0, { TRAP }); // AIL_register_EOB_callback
	Patch(SQ + 0x55190, { TRAP }); // AIL_set_sample_position
	Patch(SQ + 0x55220, { TRAP }); // AIL_sample_granularity
	Patch(SQ + 0x552F0, { TRAP }); // AIL_sample_buffer_info
	Patch(SQ + 0x553E0, { TRAP }); // AIL_load_sample_buffer
	Patch(SQ + 0x55480, { TRAP }); // AIL_sample_buffer_ready
	Patch(SQ + 0x55550, { TRAP }); // AIL_minimum_sample_buffer_size
	Patch(SQ + 0x55620, { TRAP }); // AIL_sample_pan
	Patch(SQ + 0x556F0, { TRAP }); // AIL_sample_volume
	Hook(SQ + 0x557C0, 0xE9, GetAudioSamplePlaybackRate); // AIL_sample_playback_rate
	Hook(SQ + 0x55890, 0xE9, GetAudioSampleStatus); // AIL_sample_status
	Patch(SQ + 0x55960, { 0xC3 }); // AIL_set_sample_loop_count
	Hook(SQ + 0x559F0, 0xE9, SetAudioSamplePan); // AIL_set_sample_pan
	Hook(SQ + 0x55A80, 0xE9, SetAudioSampleVolume); // AIL_set_sample_volume
	Hook(SQ + 0x55B10, 0xE9, SetAudioSamplePlaybackRate); // AIL_set_sample_playback_rate
	Hook(SQ + 0x55BA0, 0xE9, EndAudioSample); // AIL_end_sample
	Hook(SQ + 0x55C20, 0xE9, ResumeAudioSample); // AIL_resume_sample
	Hook(SQ + 0x55CA0, 0xE9, StopAudioSample); // AIL_stop_sample
	Hook(SQ + 0x55D20, 0xE9, StartAudioSample); // AIL_start_sample
	Patch(SQ + 0x55DA0, { TRAP }); // AIL_set_sample_adpcm_block_size
	Patch(SQ + 0x55E30, { TRAP }); // AIL_set_sample_type
	Patch(SQ + 0x55ED0, { TRAP }); // AIL_set_sample_address
	Patch(SQ + 0x55F70, { TRAP }); // AIL_set_digital_driver_processor
	Patch(SQ + 0x56050, { TRAP }); // AIL_set_sample_processor
	Patch(SQ + 0x56130, { TRAP }); // AIL_set_named_sample_file
	Hook(SQ + 0x56220, 0xE9, SetAudioSampleFile); // AIL_set_sample_file
	Hook(SQ + 0x562F0, 0xE9, InitAudioSample); // AIL_init_sample
	Hook(SQ + 0x56370, 0xE9, ReleaseAudioSampleHandle); // AIL_release_sample_handle
	Hook(SQ + 0x563F0, 0xE9, AllocateAudioSampleHandle); // AIL_allocate_sample_handle
	Patch(SQ + 0x564C0, { TRAP }); // AIL_release_all_timers
	Patch(SQ + 0x56530, { TRAP }); // AIL_release_timer_handle
	Patch(SQ + 0x565B0, { TRAP }); // AIL_stop_timer
	Patch(SQ + 0x56630, { TRAP }); // AIL_start_timer
	Patch(SQ + 0x566B0, { TRAP }); // AIL_set_timer_frequency
	Patch(SQ + 0x56740, { TRAP }); // AIL_set_timer_period
	Patch(SQ + 0x567D0, { TRAP }); // AIL_set_timer_user
	Patch(SQ + 0x568A0, { TRAP }); // AIL_register_timer
	Patch(SQ + 0x56980, { TRAP }); // AIL_delay
	Patch(SQ + 0x56A20, { TRAP }); // AIL_set_error
	Patch(SQ + 0x56AA0, { 0xC3 }); // AIL_set_preference
	Patch(SQ + 0x56B70, { TRAP }); // AIL_mem_free_lock
	Patch(SQ + 0x56BF0, { TRAP }); // AIL_mem_alloc_lock
	Hook(SQ + 0x56CC0, 0xE9, ShutdownAudioSystem); // AIL_shutdown
	Patch(SQ + 0x56DB0, { TRAP }); // AIL_MMX_available
	Hook(SQ + 0x57690, 0xE9, StartupAudioSystem); // AIL_startup
	Patch(SQ + 0x66C90, { 0xC3 }); // AIL_vmm_unlock
	Patch(SQ + 0x66CB0, { 0xC3 }); // AIL_vmm_lock
	Patch(SQ + 0x66CD0, { 0xC3 }); // AIL_vmm_unlock_range
	Patch(SQ + 0x66D80, { 0xC3 }); // AIL_vmm_lock_range
	Patch(SQ + 0x9C830, { TRAP }); // AIL_start
	Patch(SQ + 0x9D860, { TRAP }); // AIL_end
}

void DecodeDviImaAdpcmBlock(const uint8_t*& blockPtr, bool stereo, uint16_t nibblesPerBlock, int16_t*& samplePtr)
{
	struct Decoder
	{
		int32_t predictor;
		int32_t index;

		Decoder(const uint8_t*& ptr)
		{
			predictor = *ptr++;
			predictor |= *ptr++ << 8;
			if(predictor & 0x8000) predictor -= 0x10000;
			index = std::clamp<uint8_t>(*ptr++, 0, 88);
			++ptr;
		}

		int16_t Sample(uint8_t sample)
		{
			static const int16_t StepTable[89] =
			{
				7, 8, 9, 10, 11, 12, 13,
				14, 16, 17, 19, 21, 23, 25, 28,
				31, 34, 37, 41, 45, 50, 55, 60, 
				66, 73, 80, 88, 97, 107, 118, 
				130, 143, 157, 173, 190, 209, 230, 
				253, 279, 307, 337, 371, 408, 449, 
				494, 544, 598, 658, 724, 796, 876, 
				963, 1060, 1166, 1282, 1411, 1552, 
				1707, 1878, 2066, 2272, 2499, 2749, 
				3024, 3327, 3660, 4026, 4428, 4871, 
				5358, 5894, 6484, 7132, 7845, 8630, 
				9493, 10442, 11487, 12635, 13899, 
				15289, 16818, 18500, 20350, 22385, 
				24623, 27086, 29794, 32767
			};

			static const int8_t IndexTable[16] =
			{
				-1, -1, -1, -1, 2, 4, 6, 8,
				-1, -1, -1, -1, 2, 4, 6, 8
			};

			int32_t step = StepTable[index];
			int32_t difference = step >> 3;
			if(sample & 4) difference += step;
			if(sample & 2) difference += step >> 1;
			if(sample & 1) difference += step >> 2;
			if(sample & 8) difference = -difference;
			predictor = std::clamp(predictor + difference, -32768, 32767);
			index = std::clamp(index + IndexTable[sample], 0, 88);
			return predictor;
		}
	};

	if(stereo)
	{
		Decoder left(blockPtr);
		Decoder right(blockPtr);

		for(uint16_t i = 0; i < nibblesPerBlock; i += 16)
		{
			for(uint8_t j = 0; j < 4; ++j)
			{
				samplePtr[j * 4 + 0] = left.Sample(blockPtr[j] & 0xF);
				samplePtr[j * 4 + 2] = left.Sample(blockPtr[j] >> 4);
			}
			blockPtr += 4;
		
			for(uint8_t j = 0; j < 4; ++j)
			{
				samplePtr[j * 4 + 1] = right.Sample(blockPtr[j] & 0xF);
				samplePtr[j * 4 + 3] = right.Sample(blockPtr[j] >> 4);
			}
			blockPtr += 4;
		
			samplePtr += 16;
		}
	}
	else
	{
		Decoder decoder(blockPtr);

		for(uint16_t i = 0; i < nibblesPerBlock; i += 2)
		{
			*samplePtr++ = decoder.Sample(*blockPtr & 0xF);
			*samplePtr++ = decoder.Sample(*blockPtr++ >> 4);
		}
	}
}
