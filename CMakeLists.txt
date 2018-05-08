cmake_minimum_required(VERSION 3.8)

project(transcoding)

# Dependencies
##############
find_package(PkgConfig REQUIRED)
foreach(m avformat avcodec avfilter avutil swscale swresample)
    string(TOUPPER ${m} M)
    pkg_check_modules(${m} REQUIRED IMPORTED_TARGET lib${m})
    list(APPEND FFmpeg_LIBS PkgConfig::${m})
endforeach(m)

find_package(OpenCV)

set(CMAKE_EXTRA_INCLUDE_FILES libavcodec/avcodec.h)

add_executable(${PROJECT_NAME}
        #draw_mv.cpp
        transcoding.cpp
        )

target_link_libraries(${PROJECT_NAME} PRIVATE ${FFmpeg_LIBS} ${OpenCV_LIBS})