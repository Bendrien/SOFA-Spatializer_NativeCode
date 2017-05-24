cmake_minimum_required(VERSION 3.7)
project(SOFA_Spatializer_NativeCode)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/AudioPluginInterface.h
        src/AudioPluginUtil.cpp
        src/AudioPluginUtil.h
        src/Plugin_Gain.cpp
        src/Plugin_SofaSpatializer.cpp
        src/PluginList.h
        src/FFTConvolver/AudioFFT.cpp
        src/FFTConvolver/AudioFFT.h
        src/FFTConvolver/TwoStageFFTConvolver.cpp
        src/FFTConvolver/TwoStageFFTConvolver.h
        src/FFTConvolver/FFTConvolver.cpp
        src/FFTConvolver/FFTConvolver.h
        src/FFTConvolver/Utilities.cpp
        src/FFTConvolver/Utilities.h
        )

INCLUDE_DIRECTORIES(dep/inc)
LINK_DIRECTORIES(dep/lib)
add_library(SofaSpatializer SHARED ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(SofaSpatializer fftw3f-3 mysofa)


#########################################
### Unity
#########################################

# must use a filename that has the prefix 'AudioPlugin' in order for Unity to load the plugin
set_target_properties(SofaSpatializer PROPERTIES PREFIX "AudioPlugin")

# Check for Unityproject
if (EXISTS "../../Assets")
    set(LIBRARY_OUTPUT_PATH "../../Assets/Plugins/x86_64")
endif()
