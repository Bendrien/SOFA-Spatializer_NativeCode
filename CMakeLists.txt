cmake_minimum_required(VERSION 3.7)
project(SOFA_Spatializer_NativeCode)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        lib/AudioPluginInterface.h
        lib/AudioPluginUtil.cpp
        lib/AudioPluginUtil.h
        lib/Plugin_Gain.cpp
        lib/Plugin_SofaSpatializer.cpp
        lib/PluginList.h
        lib/FFTConvolver/AudioFFT.cpp
        lib/FFTConvolver/AudioFFT.h
        lib/FFTConvolver/FFTConvolver.cpp
        lib/FFTConvolver/FFTConvolver.h
        lib/FFTConvolver/Utilities.cpp
        lib/FFTConvolver/Utilities.h
        )

add_library(SofaSpatializer SHARED ${SOURCE_FILES})


#########################################
### Unity
#########################################

# must use a filename that has the prefix 'AudioPlugin' in order for Unity to load the plugin
set_target_properties(SofaSpatializer PROPERTIES PREFIX "AudioPlugin")

# Check for Unityproject
if (EXISTS "../../Assets")
    set(LIBRARY_OUTPUT_PATH "../../Assets/Plugins/x86_64")
endif()


#########################################
### Libraries
#########################################
