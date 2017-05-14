cmake_minimum_required(VERSION 3.7)
project(NativeCode)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        Plugin_Gain.cpp
        AudioPluginInterface.h
        AudioPluginUtil.cpp
        AudioPluginUtil.h
        PluginList.h Plugin_SofaSpatializer.cpp)

add_library(SofaSpatializer SHARED ${SOURCE_FILES})

if (WIN32)
    # must use a filename that has the prefix 'AudioPlugin' in order for Unity to load the plugin
    set_target_properties(SofaSpatializer PROPERTIES PREFIX "AudioPlugin")
    set(LIBRARY_OUTPUT_PATH ../../Assets/Plugins)
endif()