add_executable(Trader "Trader.cpp")
set_target_properties(Trader
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
target_compile_options(Trader PRIVATE -Wno-switch -Wno-format)
target_include_directories(Trader PRIVATE ${TWSAPI_INC})
target_link_libraries(Trader PRIVATE "-lpthread" client spdlog::spdlog spdlog::spdlog_header_only)