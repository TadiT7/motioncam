#include "motioncam/RawContainer.h"
#include "motioncam/RawContainerImpl.h"
#include "motioncam/RawContainerImpl_Legacy.h"
#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"

namespace motioncam {
    std::unique_ptr<RawContainer> determineContainerType(FILE* file) {
        Header header;
        
        size_t result = fread(&header, sizeof(Header), 1, file);
        if(result != 1) {
            fclose(file);
            throw IOException("Failed to determine type of container");
        }
        
        if(memcmp(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID)) != 0) {
            fclose(file);
            throw IOException("Invalid header id");
        }
        
        // Reset file position
        rewind(file);
        
        // Current version of container
        if(header.version == CONTAINER_VERSION) {
            return std::unique_ptr<RawContainerImpl>(new RawContainerImpl(file));
        }
        // Legacy container
        else if(header.version == 1) {
            return std::unique_ptr<RawContainerImpl_Legacy>(new RawContainerImpl_Legacy(file));
        }
        
        fclose(file);

        throw IOException("Unrecognised container format");
    }

    std::unique_ptr<RawContainer> RawContainer::Open(const int fd) {
        return nullptr;
    }

    std::unique_ptr<RawContainer> RawContainer::Open(const std::string& inputPath) {
        if(util::EndsWith(inputPath, "zip")) {
            return std::unique_ptr<RawContainerImpl_Legacy>(new RawContainerImpl_Legacy(inputPath));
        }
        else if(util::EndsWith(inputPath, "container")) {
            FILE* file = fopen(inputPath.c_str(), "rb");
            if(!file)
                throw IOException("Failed to open container");
            
            return determineContainerType(file);
        }
        else {
            throw IOException("Invalid file type");
        }
    }

    std::unique_ptr<RawContainer> RawContainer::Create(const int fd,
                                                       const RawCameraMetadata& cameraMetadata,
                                                       const int numSegments,
                                                       const json11::Json& extraData)
    {
        return std::unique_ptr<RawContainerImpl>(new RawContainerImpl(fd,
                                                                      cameraMetadata,
                                                                      numSegments,
                                                                      extraData));
    }

    std::unique_ptr<RawContainer> RawContainer::Create(const RawCameraMetadata& cameraMetadata,
                                                       const int numSegments,
                                                       const json11::Json& extraData)
    {
        return std::unique_ptr<RawContainerImpl>(new RawContainerImpl(cameraMetadata, numSegments, extraData));
    }
}
