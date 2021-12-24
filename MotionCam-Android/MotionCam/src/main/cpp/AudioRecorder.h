#ifndef MOTIONCAM_ANDROID_AUDIORECORDER_H
#define MOTIONCAM_ANDROID_AUDIORECORDER_H

#include <motioncam/AudioInterface.h>

#include <oboe/Oboe.h>
#include <vector>

class AudioRecorder : public motioncam::AudioInterface, public oboe::AudioStreamDataCallback {
public:
    AudioRecorder(int audioDeviceId);
    ~AudioRecorder();

    bool start(const int sampleRateHz, const int channels);
    void stop();

    int getSampleRate() const;
    int getChannels() const;
    const std::vector<int16_t>& getAudioData(uint32_t& outNumFrames) const;

private:
    void destroy();

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames);

private:
    std::shared_ptr<oboe::AudioStream> mActiveAudioStream;
    std::atomic<bool> mRunning;
    std::vector<int16_t> mAudioData;
    size_t mAudioDataOffset;
    int mSampleRate;
    int mChannelCount;
    int mAudioDeviceId;
};

#endif
