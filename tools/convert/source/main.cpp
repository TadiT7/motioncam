#include <iostream>
#include <motioncam/MotionCam.h>

class ProgressListener : public motioncam::ImageProcessorProgress {
public:
    bool onProgressUpdate(int progress) const {
        std::cout << "Progress update: " << progress << std::endl;
        return true;
    }
    
    std::string onPreviewSaved(const std::string& outputPath) const {
        return "";
    }
    
    void onCompleted() const {
        
    }
    
    void onError(const std::string& error) const {
        
    }
};

void printHelp() {
    std::cout << "Usage: convert [-t] [-I] file.zip /output/path" << std::endl << std::endl;
    std::cout << "-t\tNumber of threads" << std::endl;
    std::cout << "-I\tProcess as image" << std::endl;
}

int main(int argc, const char* argv[]) {    
    if(argc < 3) {
        printHelp();
        return 1;
    }
    
    int numThreads = 4;
    bool processAsImage = false;
    
    int i = 1;
    
    for(; i < argc; i++) {
        if(std::string(argv[i]) == "-t") {
            if(i + 1 >= argc) {
                printHelp();
                exit(1);
            }
            
            numThreads = std::stoi(argv[i+1]);
            ++i;
        }
        else if(std::string(argv[i]) == "-I") {
            processAsImage = true;
        }
        else {
            break;
        }
    }
    
    if(i + 1 > argc) {
        printHelp();
        exit(1);
    }
    
    std::string inputFile = argv[i];
    std::string outputPath = argv[i+1];
    
    if(outputPath[outputPath.size() - 1] == '/' ||
       outputPath[outputPath.size() - 1] == '\\' )
    {
        outputPath.resize(outputPath.size() - 1);
    }
    
    try {
        if(processAsImage) {
            ProgressListener progressListener;
            
            motioncam::ProcessImage(inputFile, outputPath, progressListener);
        }
        else {
            std::cout << "Using " << numThreads << " threads" << std::endl;
            
            motioncam::ConvertVideoToDNG(inputFile, outputPath, numThreads);
        }
    }
    catch(std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
