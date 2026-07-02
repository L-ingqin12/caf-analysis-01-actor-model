cmake_minimum_required(VERSION 3.19)

# -- project -------------------------------------------------------------------

project(actor_model_demo VERSION 1.0.0 LANGUAGES CXX)

# -- C++ standard --------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -- CAF (C++ Actor Framework) via FetchContent --------------------------------

include(FetchContent)

# Pin to the same version analyzed in the study.
set(CAF_VERSION_TAG "v1.1.0")

FetchContent_Declare(
  CAF
  GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
  GIT_TAG        ${CAF_VERSION_TAG}
  GIT_SHALLOW    TRUE
)

# Let CAF know it is consumed as a sub-project so it doesn't install itself.
set(CAF_BUILD_EXAMPLES OFF CACHE BOOL "Disable building of CAF examples")
set(CAF_BUILD_TESTING  OFF CACHE BOOL "Disable building of CAF tests")

FetchContent_MakeAvailable(CAF)

# -- demo executable -----------------------------------------------------------

add_executable(actor_model_demo main.cpp)

target_link_libraries(actor_model_demo PRIVATE CAF::core)

# Copy the config file next to the binary so the actor system can find it.
configure_file(
  caf-application.conf
  ${CMAKE_CURRENT_BINARY_DIR}/caf-application.conf
  COPYONLY
)
