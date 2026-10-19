cmake_minimum_required(VERSION 3.20)
project(ArteryDatasetGenerator LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WITH_SCENARIOS "Build Artery with scenarios" OFF)
option(WITH_STORYBOARD "Build Artery with storyboard feature" OFF)
option(WITH_ENVMOD "Build Artery with environment model feature" ON)
add_subdirectory(artery)

add_artery_feature(dataset
    src/StationRecorder.cc
    src/DatasetSink.cc
)
set_target_properties(dataset PROPERTIES NED_FOLDERS ${PROJECT_SOURCE_DIR}/config)
target_include_directories(dataset PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/libs/v2x-misc/include)
target_link_libraries(dataset PRIVATE envmod)

add_opp_run(dataset CONFIG omnetpp.ini WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
