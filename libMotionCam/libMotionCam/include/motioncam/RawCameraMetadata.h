#ifndef RawCameraMetadata_h
#define RawCameraMetadata_h

#include <vector>
#include <opencv2/opencv.hpp>
#include <json11/json11.hpp>

#include "motioncam/Types.h"
#include "motioncam/Color.h"


namespace motioncam {
    struct RawImageMetadata;

    struct RawCameraMetadata {
        RawCameraMetadata(const json11::Json& json);
        RawCameraMetadata();
        
        ColorFilterArrangment sensorArrangment;

        cv::Mat colorMatrix1;
        cv::Mat colorMatrix2;
        
        cv::Mat calibrationMatrix1;
        cv::Mat calibrationMatrix2;

        cv::Mat forwardMatrix1;
        cv::Mat forwardMatrix2;

        color::Illuminant colorIlluminant1;
        color::Illuminant colorIlluminant2;
      
        std::vector<float> apertures;
        std::vector<float> focalLengths;
        
        const std::vector<float>& getBlackLevel(const RawImageMetadata& bufferMetadata) const;
        const std::vector<float>& getBlackLevel() const;
        
        const float getWhiteLevel(const RawImageMetadata& bufferMetadata) const;
        const float getWhiteLevel() const;
        
        void updateBayerOffsets(const std::vector<float>& blackLevel, const float whiteLevel);
        void toJson(json11::Json::object& metadataJson);
        
    private:
        void parse(const json11::Json& metadata);
        
        float whiteLevel;
        std::vector<float> blackLevel;
    };
}

#endif /* RawCameraMetadata_h */
