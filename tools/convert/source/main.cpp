#include <iostream>
#include <motioncam/MotionCam.h>

void printHelp() {
    std::cout << "Usage: convert file.zip /output/path" << std::endl;
}

int main(int argc, const char* argv[]) {
    
    if(argc < 3) {
        printHelp();
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputPath = argv[2];
    
    if(outputPath[outputPath.size() - 1] == '/' ||
       outputPath[outputPath.size() - 1] == '\\' )
    {
        outputPath.resize(outputPath.size() - 1);
    }
    
    try {
        motioncam::ConvertVideoToDNG(inputFile, outputPath);
    }
    catch(std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        printHelp();
        
        return 1;
    }
    
    return 0;
}
