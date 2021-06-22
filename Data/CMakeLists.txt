add_executable(DataHarvester "DataHarvest.cpp")
set_target_properties(DataHarvester
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)

target_compile_options(DataHarvester PRIVATE -Wno-switch -Wno-format)
target_include_directories(DataHarvester PRIVATE ${CLIENT_INCLUDE_DIRS} ${TWSAPI_INCLUDE_DIRS})
if( ${CMAKE_BUILD_TYPE} STREQUAL "DEBUG" OR ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_link_libraries(DataHarvester PRIVATE ${TWSAPI_LIBRARIES} twsapid TradeBase client spdlog::spdlog spdlog::spdlog_header_only)
else()
    target_link_libraries(DataHarvester PRIVATE ${TWSAPI_LIBRARIES} twsapi ${TRADEBASE_LIBRARIES} client spdlog::spdlog spdlog::spdlog_header_only)
endif()