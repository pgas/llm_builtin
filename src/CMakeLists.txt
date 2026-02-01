# Build llm.so shared library (Bash builtin module)

# Get the bash headers directory
find_path(BASH_HEADERS_FOUND builtins.h
    HINTS "${BASH_HEADERS}/bash" "${BASH_HEADERS}" "${BASH_HEADERS}/include"
    NO_DEFAULT_PATH
)

if(NOT BASH_HEADERS_FOUND)
    message(FATAL_ERROR "Unable to find the bash headers. Please use -DBASH_HEADERS=/path/to/headers/")
endif()

# Find curl package
find_package(CURL REQUIRED)

# Add nlohmann/json via FetchContent
include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)

# Create the shared library
add_library(llm MODULE llm.cpp)

# Link against curl and nlohmann/json
target_link_libraries(llm PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)


# Set library properties
set_target_properties(llm PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

# Add include directories for bash headers
target_include_directories(llm PRIVATE
    "${BASH_HEADERS}/bash"
    "${BASH_HEADERS}/bash/include"
    "${BASH_HEADERS}/bash/builtins"
    "${BASH_HEADERS}"
    "${BASH_HEADERS}/include"
    "${BASH_HEADERS}/builtins"
)

# Install the shared library to lib/bas
install(TARGETS llm
    LIBRARY DESTINATION lib/bas
)
