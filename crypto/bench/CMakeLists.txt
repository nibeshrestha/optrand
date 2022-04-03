set(pvss_bench_sources
)

foreach(appSrc ${pvss_bench_sources})
    get_filename_component(appName ${appSrc} NAME_WE)
    set(appDir ../bin/bench)

    add_executable(${appName} ${appSrc})
    target_link_libraries(${appName} PRIVATE pvss)

    set_target_properties(${appName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${appDir})
endforeach()