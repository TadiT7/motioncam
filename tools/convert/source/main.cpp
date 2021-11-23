#include <iostream>
#include <iomanip>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    
    int onNeedFd(int frameNumber) const {
        std::ostringstream str;

        str << std::setw(6) << std::setfill('0') << frameNumber;

        std::string outputDngPath = outputPath + "/frame-" + str.str() + ".dng";

        return open(outputDngPath.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP);
    }
    
    void onCompleted(int fd) const {
    }
    
    bool onProgressUpdate(int progress) const {
        std::cout << progress << "%" << std::endl;
        return true;
    }
    
    void onCompleted() const {
        std::cout << "DONE" << std::endl;
    }
    
    void onError(const std::string& error) const {
        std::cout << "ERROR: " << error << std::endl;
    }
    
private:
    std::string outputPath;
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
        std::cout << "Opening " << inputFile << std::endl;

        if(processAsImage) {
            ProgressListener progressListener;
            
            motioncam::ProcessImage(inputFile, outputPath, progressListener);
        }
        else {
            DngOutputListener listener(outputPath);

            std::cout << "Using " << numThreads << " threads" << std::endl;

            motioncam::ConvertVideoToDNG(inputFile, listener, numThreads);
        }
    }
    catch(std::runtime_error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
