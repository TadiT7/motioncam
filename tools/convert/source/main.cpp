#include <iostream>
#include <iomanip>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <motioncam/MotionCam.h>

class ProgressListener : public motioncam::ImageProcessorProgress {
public:
    bool onProgressUpdate(int progress) const {
        std::cout << progress << "%" << std::endl;
        return true;
    }
    
    std::string onPreviewSaved(const std::string& outputPath) const {
        return "{}";
    }
    
    void onCompleted() const {
        std::cout << "DONE" << std::endl;
    }
    
    void onError(const std::string& error) const {
        std::cout << "ERROR: " << error << std::endl;
    }
};

class DngOutputListener : public motioncam::DngProcessorProgress {
public:
    DngOutputListener(const std::string& outputPath) : outputPath(outputPath) {
    }
    
    int onNeedFd(int frameNumber) {
        std::ostringstream str;

        str << std::setw(6) << std::setfill('0') << frameNumber;

        std::string outputDngPath = outputPath + "/frame-" + str.str() + ".dng";

        std::cout << "Creating " << outputDngPath << std::endl;
        
        return open(outputDngPath.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP);
    }
    
    bool onProgressUpdate(int progress) {
        std::cout << progress << "%" << std::endl;
        return true;
    }
    
    void onCompleted() {
        std::cout << "DONE" << std::endl;
    }
    
    void onError(const std::string& error) {
        std::cout << "ERROR: " << error << std::endl;
    }
    
private:
    std::string outputPath;
};

void printHelp() {
    std::cout << "Usage: convert [-t] [-I] -i file.0.container -i file.1.container -i ... /output/path" << std::endl << std::endl;
    std::cout << "-t\tNumber of threads" << std::endl;
    std::cout << "-n\tNumber of frames to merge" << std::endl;
    std::cout << "-p\tProcess as image" << std::endl;
}

int main(int argc, const char* argv[]) {    
    if(argc < 3) {
        printHelp();
        return 1;
    }

    std::vector<std::string> inputs;
    int numThreads = 4;
    bool processAsImage = false;
    int numFramesToMerge = 0;
    
    int i = 1;
    std::string outputPath;
    
    for(; i < argc; i++) {
        if(std::string(argv[i]) == "-t") {
            if(i + 1 >= argc) {
                printHelp();
                exit(1);
            }
            
            numThreads = std::stoi(argv[i+1]);
            ++i;
        }
        else if(std::string(argv[i]) == "-n") {
            if(i + 1 >= argc) {
                printHelp();
                exit(1);
            }
            
            numFramesToMerge = std::stoi(argv[i+1]);
            ++i;
        }
        else if(std::string(argv[i]) == "-i") {
            if(i + 1 >= argc) {
                printHelp();
                exit(1);
            }

            inputs.push_back(argv[i+1]);
            ++i;
        }

        else if(std::string(argv[i]) == "-p") {
            processAsImage = true;
        }
        else if(i == argc - 1) {
            outputPath = argv[i];
            break;
        }
    }
    
    if(outputPath.empty()) {
        std::cerr << "No output path" << std::endl;
        printHelp();
        exit(1);
    }

    
    if(outputPath[outputPath.size() - 1] == '/' ||
       outputPath[outputPath.size() - 1] == '\\' )
    {
        outputPath.resize(outputPath.size() - 1);
    }
    
    if(inputs.size() == 0) {
        std::cerr << "No inputs" << std::endl;
        printHelp();
        exit(1);
    }
    
    try {
        if(processAsImage) {
            for(size_t i = 0; i < inputs.size(); i++) {
                std::cout << "Opening " << inputs[i] << std::endl;

                ProgressListener progressListener;
            
                motioncam::MotionCam::ProcessImage(inputs[i], outputPath, progressListener);
            }
        }
        else {
            DngOutputListener listener(outputPath);
            motioncam::MotionCam m;

            std::cout << "Using " << numThreads << " threads" << std::endl;
            std::cout << "Merging " << numFramesToMerge << " frames" << std::endl;
            
            m.convertVideoToDNG(inputs, listener, motioncam::NO_DENOISE_WEIGHTS, numThreads, numFramesToMerge, true, true);
        }
    }
    catch(std::runtime_error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
