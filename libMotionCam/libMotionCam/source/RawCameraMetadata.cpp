#include "motioncam/RawCameraMetadata.h"
#include "motioncam/Util.h"

namespace motioncam {

    RawCameraMetadata::RawCameraMetadata(const json11::Json& json) {
        parse(json);
    }

    RawCameraMetadata::RawCameraMetadata() :
        sensorArrangment(ColorFilterArrangment::RGGB),
        colorIlluminant1(color::StandardA),
        colorIlluminant2(color::D50),
        whiteLevel(0)
    {
    }

    void RawCameraMetadata::parse(const json11::Json& metadata) {
        // Black/white levels
        std::vector<float> blackLevel;
        
        std::vector<json11::Json> blackLevelValues = metadata["blackLevel"].array_items();
        for(auto& blackLevelValue : blackLevelValues) {
            blackLevel.push_back(blackLevelValue.number_value());
        }
        
        int whiteLevel = util::GetRequiredSettingAsInt(metadata, "whiteLevel");
        
        // Default to 64
        if(blackLevel.empty()) {
            for(int i = 0; i < 4; i++)
                blackLevel.push_back(64);
        }

        // Default to 1023
        if(whiteLevel <= 0)
            whiteLevel = 1023;

        this->updateBayerOffsets(blackLevel, whiteLevel);
        
        // Color arrangement
        std::string colorFilterArrangment = util::GetRequiredSettingAsString(metadata, "sensorArrangment");

        if(colorFilterArrangment == "grbg") {
            this->sensorArrangment = ColorFilterArrangment::GRBG;
        }
        else if(colorFilterArrangment == "gbrg") {
            this->sensorArrangment = ColorFilterArrangment::GBRG;
        }
        else if(colorFilterArrangment == "bggr") {
            this->sensorArrangment = ColorFilterArrangment::BGGR;
        }
        else if(colorFilterArrangment == "rgb") {
            this->sensorArrangment = ColorFilterArrangment::RGB;
        }
        else if(colorFilterArrangment == "mono") {
            this->sensorArrangment = ColorFilterArrangment::MONO;
        }
        else {
            // Default to RGGB
            this->sensorArrangment = ColorFilterArrangment::RGGB;
        }
        
        // Matrices
        this->colorIlluminant1 = color::IlluminantFromString(metadata["colorIlluminant1"].string_value());
        this->colorIlluminant2 = color::IlluminantFromString(metadata["colorIlluminant2"].string_value());

        this->colorMatrix1 = util::toMat3x3(metadata["colorMatrix1"].array_items());
        this->colorMatrix2 = util::toMat3x3(metadata["colorMatrix2"].array_items());

        this->calibrationMatrix1 = util::toMat3x3(metadata["calibrationMatrix1"].array_items());
        this->calibrationMatrix2 = util::toMat3x3(metadata["calibrationMatrix2"].array_items());

        this->forwardMatrix1 = util::toMat3x3(metadata["forwardMatrix1"].array_items());
        this->forwardMatrix2 = util::toMat3x3(metadata["forwardMatrix2"].array_items());

        // Misc
        if(metadata["apertures"].is_array()) {
            for(size_t i = 0; i < metadata["apertures"].array_items().size(); i++)
                this->apertures.push_back(metadata["apertures"].array_items().at(i).number_value());
        }

        if(metadata["focalLengths"].is_array()) {
            for(size_t i = 0; i < metadata["focalLengths"].array_items().size(); i++)
                this->focalLengths.push_back(metadata["focalLengths"].array_items().at(i).number_value());
        }
    }

    const std::vector<float>& RawCameraMetadata::getBlackLevel(const RawImageMetadata& bufferMetadata) const {
        return blackLevel; // TODO: Don't use dynamic black level. Seems broken.
    //            return bufferMetadata.dynamicBlackLevel.empty() ? this->blackLevel : bufferMetadata.dynamicBlackLevel;
    }

    const std::vector<float>& RawCameraMetadata::getBlackLevel() const {
        return blackLevel;
    }

    const float RawCameraMetadata::getWhiteLevel(const RawImageMetadata& bufferMetadata) const {
        return this->whiteLevel; // TODO: Don't use dynamic white level. Seems broken.
        //return bufferMetadata.dynamicWhiteLevel <= 0 ? this->whiteLevel : bufferMetadata.dynamicWhiteLevel;
    }

    const float RawCameraMetadata::getWhiteLevel() const {
        return whiteLevel;
    }

    void RawCameraMetadata::updateBayerOffsets(const std::vector<float>& blackLevel, const float whiteLevel) {
        this->blackLevel = blackLevel;
        this->whiteLevel = whiteLevel;
    }

    void RawCameraMetadata::toJson(json11::Json::object& metadataJson) {
        metadataJson["colorIlluminant1"]    = color::IlluminantToString(colorIlluminant1);
        metadataJson["colorIlluminant2"]    = color::IlluminantToString(colorIlluminant2);
        metadataJson["forwardMatrix1"]      = util::toJsonArray(forwardMatrix1);
        metadataJson["forwardMatrix2"]      = util::toJsonArray(forwardMatrix2);
        metadataJson["colorMatrix1"]        = util::toJsonArray(colorMatrix1);
        metadataJson["colorMatrix2"]        = util::toJsonArray(colorMatrix2);
        metadataJson["calibrationMatrix1"]  = util::toJsonArray(calibrationMatrix1);
        metadataJson["calibrationMatrix2"]  = util::toJsonArray(calibrationMatrix2);
        metadataJson["blackLevel"]          = getBlackLevel();
        metadataJson["whiteLevel"]          = getWhiteLevel();
        metadataJson["sensorArrangment"]    = util::toString(sensorArrangment);
        metadataJson["apertures"]           = apertures;
        metadataJson["focalLengths"]        = focalLengths;
    }
}
