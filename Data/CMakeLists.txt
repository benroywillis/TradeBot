add_executable(DataHarvester "DataHarvest.cpp")
set_target_properties(DataHarvester
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
target_compile_options(DataHarvester PRIVATE -Wno-switch -Wno-format)
target_include_directories(DataHarvester PRIVATE )
target_link_libraries(DataHarvester PRIVATE "-lpthread" client spdlog::spdlog spdlog::spdlog_header_only)