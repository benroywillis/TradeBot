file(GLOB SOURCES "*.cpp" )
add_library(client SHARED ${SOURCES})
set_target_properties(client
    PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
target_compile_options(client PRIVATE -fPIC)
target_include_directories(client PRIVATE "inc/")
target_include_directories(client SYSTEM PRIVATE ${TWSAPI_INC})
target_link_libraries(client PRIVATE "/usr/lib/libtwsapi.a" spdlog::spdlog spdlog::spdlog_header_only)
install(TARGETS client
    LIBRARY DESTINATION lib
)