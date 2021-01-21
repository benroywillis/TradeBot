file(GLOB SOURCES "*.cpp" )
add_library(client SHARED ${SOURCES})
set_target_properties(client
    PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
target_include_directories(client PRIVATE "inc/" ${TWSAPI_INCLUDE_DIRS})
target_link_libraries(client PRIVATE ${TWSAPI_LIBRARIES} spdlog::spdlog spdlog::spdlog_header_only)
install(TARGETS client
    LIBRARY DESTINATION lib
)