add_library(mvrvb_common STATIC
    compatibility_catalog.cpp
    compatibility_profile.cpp
    logging.cpp
    runtime_launch_command.cpp
    runtime_launch_plan.cpp
    runtime_setup_command.cpp
    threading.cpp
)

target_include_directories(mvrvb_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mvrvb_common PUBLIC cxx_std_20)

target_compile_options(mvrvb_common PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
)
