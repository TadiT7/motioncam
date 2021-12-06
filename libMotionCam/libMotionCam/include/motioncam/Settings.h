#ifndef Settings_h
#define Settings_h

#include <string>
#include <map>

namespace json11 {
    class Json;
}

namespace motioncam {
    float getSetting(const json11::Json& json, const std::string& key, const float defaultValue);
    int getSetting(const json11::Json& json, const std::string& key, const int defaultValue);
    bool getSetting(const json11::Json& json, const std::string& key, const bool defaultValue);
    std::string getSetting(const json11::Json& json, const std::string& key, const std::string& defaultValue);

    struct PostProcessSettings {
        // Denoising
        int spatialDenoiseLevel;

        // Post processing
        float temperature;
        float tint;

        float gamma;
        float tonemapVariance;
        float shadows;
        float whitePoint;
        float contrast;
        float brightness;
        float sharpen0;
        float sharpen1;
        float pop;
        float blacks;
        float exposure;
        float clippedLows;
        float clippedHighs;
        
        float noiseSigma;
        float sceneLuminance;
        
        float saturation;
        float blues;
        float greens;
        
        int jpegQuality;
        bool flipped;
        bool dng;

        float gpsLatitude;
        float gpsLongitude;
        float gpsAltitude;
        std::string gpsTime;

        std::string captureMode;
        
        PostProcessSettings();
        PostProcessSettings(const json11::Json& json);
        
        void toJson(std::map<std::string, json11::Json>& json) const;
    };
}

#endif /* Settings_h */
