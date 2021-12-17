#ifndef AudioInterface_h
#define AudioInterface_h

#include <cstdint>
#include <vector>

namespace motioncam {
    class AudioInterface {
    public:
        virtual bool start(const int sampleRateHz, const int channels) = 0;
        virtual void stop() = 0;
        virtual const std::vector<int16_t>& getAudioData(uint32_t& outNumFrames) const = 0;
        
        virtual int getSampleRate() const = 0;
        virtual int getChannels() const = 0;
    };
}

#endif /* AudioInterface_h */
