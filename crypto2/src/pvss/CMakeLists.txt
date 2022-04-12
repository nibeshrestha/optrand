add_library(pvss
    PVSS.cpp
)

target_link_libraries(pvss 
    ff
    zm
    gmp
    gmpxx
)

target_include_directories(pvss INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>"
    "$<INSTALL_INTERFACE:${INSTALL_CMAKE_DIR}/include>"
)
