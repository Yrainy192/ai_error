cmake_minimum_required(VERSION 3.10)

project(rknn_yolov8_demo)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()


set(rknpu_yolov8_file rknpu2/yolov8.cc)
set(RKNNRT_LIB /usr/lib/librknnrt.so)  #20260210 wdh add

# ===== 头文件 =====
include_directories(
    ${SDK_INCLUDE}
)

if (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103")
    add_definitions(-DRV1106_1103)
    set(rknpu_yolov8_file rknpu2/yolov8_rv1106_1103.cc)
    #dma
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/allocator/dma)
endif()

if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    add_definitions(-DRKNPU1)
    set(rknpu_yolov8_file rknpu1/yolov8.cc)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)


set(EIGEN3_INCLUDE_DIR /usr/include/eigen3)
include_directories(${EIGEN3_INCLUDE_DIR})

add_executable(${PROJECT_NAME}
    main.cc
    postprocess.cc
    rknpu2/yolov8.cc
    ${rknpu_yolov8_file}
)




find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
find_package(spdlog REQUIRED)


pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
)


include_directories(${FFMPEG_INCLUDE_DIRS})
find_package(OpenCV REQUIRED)

# =========================
# GStreamer
# =========================
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0

    
)

# =========================
# GLib（关键修复点！！）
# =========================
pkg_check_modules(GLIB REQUIRED
    glib-2.0
    gobject-2.0
)

# =========================
# include
# =========================
include_directories(
    /usr/include/gstreamer-1.0
    /usr/include/glib-2.0
    /usr/lib/aarch64-linux-gnu/glib-2.0/include

    ${GST_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}

    ${OpenCV_INCLUDE_DIRS}

    /usr/include/rockchip
    /usr/include/rga
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

include_directories(${JSONCPP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${JSONCPP_LIBRARIES})
# =========================
# link dirs
# =========================
link_directories(
    /usr/lib/aarch64-linux-gnu
)

target_link_libraries(${PROJECT_NAME}
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}

    gstapp-1.0
    gstvideo-1.0
    gstreamer-1.0

    gobject-2.0
    glib-2.0

    rockchip_mpp
    rga

    ${OpenCV_LIBS}

    pthread
    dl
    ${FFMPEG_LIBRARIES}
    /usr/lib/libsdk.so
    asound
    spdlog::spdlog
)


target_link_libraries(${PROJECT_NAME}
    imageutils
    fileutils
    imagedrawing    
    ${RKNNRT_LIB} #20260210 wdh add
    ${LIBRKNNRT}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
endif()

message(STATUS "!!!!!!!!!!!CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
)


# Currently zero copy only supports rknpu2, v1103/rv1103b/rv1106 supports zero copy by default
if (NOT (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103" OR TARGET_SOC STREQUAL "rk1808" 
    OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126" OR TARGET_SOC STREQUAL "rv1103b"))
    add_executable(${PROJECT_NAME}_zero_copy
        main.cc
        postprocess.cc
        rknpu2/yolov8_zero_copy.cc
        ${BYTE_TRACK_SRC}

    )

    target_compile_definitions(${PROJECT_NAME}_zero_copy PRIVATE ZERO_COPY)


    target_link_libraries(${PROJECT_NAME}_zero_copy
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}

    gstapp-1.0
    gstvideo-1.0
    gstreamer-1.0

    gobject-2.0
    glib-2.0

    rockchip_mpp
    rga

    ${OpenCV_LIBS}

    pthread
    dl
    ${FFMPEG_LIBRARIES}
    /usr/lib/libsdk.so
    spdlog::spdlog
)
    target_link_libraries(${PROJECT_NAME}_zero_copy
        imageutils
        fileutils
        imagedrawing    
	    ${RKNNRT_LIB} #20260210 wdh add
        ${LIBRKNNRT}
        dl
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_link_libraries(${PROJECT_NAME}_zero_copy
        log
    )
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(${PROJECT_NAME}_zero_copy Threads::Threads)
    endif()

    target_include_directories(${PROJECT_NAME}_zero_copy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LIBRKNNRT_INCLUDES}
    )
    install(TARGETS ${PROJECT_NAME}_zero_copy DESTINATION .)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/bus.jpg DESTINATION model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/coco_80_labels_list.txt DESTINATION model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)


# =========================
# ByteTrack
# =========================

file(GLOB BYTE_TRACK_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/ByteTrack/*.cpp
)

target_include_directories(rknn_yolov8_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ByteTrack
)
target_sources(rknn_yolov8_demo PRIVATE
    ${BYTE_TRACK_SRC}
)

target_include_directories(rknn_yolov8_demo_zero_copy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ByteTrack
)
target_sources(rknn_yolov8_demo_zero_copy PRIVATE
    ${BYTE_TRACK_SRC}
)

target_link_libraries(rknn_yolov8_demo
    mysqlclient
    jsoncpp
    asound
)

target_link_libraries(rknn_yolov8_demo_zero_copy
    mysqlclient
    jsoncpp
    asound
)
