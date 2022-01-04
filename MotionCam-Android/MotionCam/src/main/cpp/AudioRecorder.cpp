#include "AudioRecorder.h"
#include "camera/Logger.h"

AudioRecorder::AudioRecorder(int audioDeviceId) :
    mRunning(false),
    mAudioDeviceId(audioDeviceId),
    mAudioDataOffset(0),
    mSampleRate(0),
    mChannelCount(0)
{
}

AudioRecorder::~AudioRecorder() {
    destroy();
}

bool AudioRecorder::start(const int sampleRateHz, const int channels) {
    if(mRunning)
        return false;

    oboe::AudioStreamBuilder builder;

    int deviceId = mAudioDeviceId < 0 ? oboe::kUnspecified : mAudioDeviceId;

    oboe::Result result = builder
        .setSharingMode(oboe::SharingMode::Shared)
        ->setPerformanceMode(oboe::PerformanceMode::PowerSaving)
        ->setUsage(oboe::Usage::Media)
        ->setDirection(oboe::Direction::Input)
        ->setFormat(oboe::AudioFormat::I16)
        ->setChannelCount(channels)
        ->setSampleRate(sampleRateHz)
        ->setInputPreset(oboe::InputPreset::Camcorder)
        ->setDataCallback(this)
        ->setDeviceId(deviceId)
        ->openStream(mActiveAudioStream);

    if(result != oboe::Result::OK) {
        return false;
    }

    // Set initial buffer size to 5 minutes
    const size_t bufferFrames = channels * sampleRateHz * 60 * 5;

    mAudioData.resize(bufferFrames);

    result = mActiveAudioStream->requestStart();
    if(result != oboe::Result::OK) {
        mActiveAudioStream->close();
        mActiveAudioStream.reset();
        return false;
    }

    // Keep stream type
    mSampleRate = mActiveAudioStream->getSampleRate();
    mChannelCount = mActiveAudioStream->getChannelCount();

    mRunning = true;
    return true;
}

void AudioRecorder::stop() {
    destroy();
}

void AudioRecorder::destroy() {
    if(mActiveAudioStream) {
        mActiveAudioStream->stop();
        mActiveAudioStream.reset();
    }

    mRunning = false;
}

const std::vector<int16_t>& AudioRecorder::getAudioData(uint32_t& outNumFrames) const {
    if(mRunning) {
        throw std::runtime_error("Device still running");
    }

    outNumFrames = mAudioDataOffset;

    return mAudioData;
}

int AudioRecorder::getSampleRate() const {
    return mSampleRate;
}

int AudioRecorder::getChannels() const {
    return mChannelCount;
}

oboe::DataCallbackResult AudioRecorder::onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) {
    // Resize in large chunks
    // TODO: move out of audio callback
    if(mAudioDataOffset + numFrames >= mAudioData.size()) {
        // Increase by 5 minutes at a time
        const size_t bufferFrames = mChannelCount * mSampleRate * 60 * 5;

        mAudioData.resize(mAudioData.size() + bufferFrames);
    }

    uint32_t numBytes = numFrames * audioStream->getBytesPerFrame();

    if(numBytes > 0) {
        std::memcpy(mAudioData.data() + mAudioDataOffset, audioData, numBytes);
        mAudioDataOffset += numBytes / audioStream->getBytesPerSample();
    }

    return mRunning ? oboe::DataCallbackResult::Continue : oboe::DataCallbackResult::Stop;
}