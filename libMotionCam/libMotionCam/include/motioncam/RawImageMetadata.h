#ifndef RawImageMetadata_hpp
#define RawImageMetadata_hpp

#include "motioncam/Color.h"
#include "motioncam/Settings.h"
#include "motioncam/Types.h"

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace motioncam {
    struct RawImageMetadata
    {
        RawImageMetadata() :
            dynamicWhiteLevel(0),
            exposureTime(0),
            iso(0),
            timestampNs(0),
            recvdTimestampMs(0),
            exposureCompensation(0),
            screenOrientation(ScreenOrientation::PORTRAIT),
            rawType(RawType::ZSL)
        {
        }

        RawImageMetadata(const RawImageMetadata& other) :
            dynamicBlackLevel(other.dynamicBlackLevel),
            dynamicWhiteLevel(other.dynamicWhiteLevel),
            asShot(other.asShot),
            lensShadingMap(other.lensShadingMap),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            recvdTimestampMs(other.recvdTimestampMs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType),
            noiseProfile(other.noiseProfile)
        {
        }

        RawImageMetadata(const RawImageMetadata&& other) noexcept :
            dynamicBlackLevel(std::move(other.dynamicBlackLevel)),
            dynamicWhiteLevel(other.dynamicWhiteLevel),
            asShot(std::move(other.asShot)),
            lensShadingMap(std::move(other.lensShadingMap)),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            recvdTimestampMs(other.recvdTimestampMs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType),
            noiseProfile(other.noiseProfile)
        {
        }

        RawImageMetadata& operator=(const RawImageMetadata &obj) {
            dynamicBlackLevel = obj.dynamicBlackLevel;
            dynamicWhiteLevel = obj.dynamicWhiteLevel;
            asShot = obj.asShot;
            lensShadingMap = obj.lensShadingMap;
            exposureTime = obj.exposureTime;
            iso = obj.iso;
            exposureCompensation = obj.exposureCompensation;
            timestampNs = obj.timestampNs;
            recvdTimestampMs = obj.recvdTimestampMs;
            screenOrientation = obj.screenOrientation;
            rawType = obj.rawType;
            noiseProfile = obj.noiseProfile;

            return *this;
        }

        std::vector<float> dynamicBlackLevel;
        float dynamicWhiteLevel;

        cv::Vec3f asShot;

        cv::Mat colorMatrix1;
        cv::Mat colorMatrix2;

        cv::Mat calibrationMatrix1;
        cv::Mat calibrationMatrix2;

        cv::Mat forwardMatrix1;
        cv::Mat forwardMatrix2;

        int64_t exposureTime;
        int32_t iso;
        int32_t exposureCompensation;
        int64_t timestampNs;
        int64_t recvdTimestampMs;
        ScreenOrientation screenOrientation;
        RawType rawType;
        std::vector<double> noiseProfile;
        
        void updateShadingMap(const std::vector<cv::Mat>& shadingMap) {
            this->lensShadingMap = shadingMap;
        }
        
        const std::vector<cv::Mat>& shadingMap() const {
            return lensShadingMap;
        }
        
    private:
        std::vector<cv::Mat> lensShadingMap;
    };

} // namespace motioncam

#endif /* RawImageMetadata_hpp */
