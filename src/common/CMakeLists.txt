add_library(mvrvb_common STATIC
    logging.cpp
    threading.cpp
)

target_include_directories(mvrvb_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mvrvb_common PUBLIC cxx_std_20)

target_compile_options(mvrvb_common PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
)
