# Frontend distribution directory
set(
    WEB_DIST_DIR
    "Z:/Chemecar/2026/chemecar-web-Ecust-2026/dist"
)

# Convert to absolute path
get_filename_component(
    WEB_DIST_DIR
    "${WEB_DIST_DIR}"
    ABSOLUTE
)

# Validate directory
if(NOT IS_DIRECTORY "${WEB_DIST_DIR}")
    message(FATAL_ERROR
        "Frontend distribution directory not found: ${WEB_DIST_DIR}"
    )
endif()

# Export to environment
set(ENV{WEB_DIST_DIR} "${WEB_DIST_DIR}")

message(STATUS "WEB_DIST_DIR: ${WEB_DIST_DIR}")