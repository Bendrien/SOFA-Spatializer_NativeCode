cmake_minimum_required(VERSION 3.7)
project(NativeCode)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        Plugin_Gain.cpp
        Plugin_Gain.h
        AudioPluginInterface.h
        AudioPluginUtil.cpp
        AudioPluginUtil.h
        PluginList.h)

set(LIBRARY_OUTPUT_PATH ../../Assets/Plugins)

add_library(SofaSpatializer SHARED ${SOURCE_FILES})

# must use a filename that has the prefix 'AudioPlugin' in order for Unity to load the plugin
set_target_properties(SofaSpatializer PROPERTIES PREFIX "AudioPlugin")