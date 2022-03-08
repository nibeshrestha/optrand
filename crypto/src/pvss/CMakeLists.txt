add_library(pvss
    PVSS.cpp
)

target_link_libraries(pvss 
    ff
)

target_include_directories(pvss PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>"
    "$<INSTALL_INTERFACE:${INSTALL_CMAKE_DIR}/include>"
)
