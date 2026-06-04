cmake_minimum_required(VERSION 3.16)
project(CS100_ATM_Simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "The secure ATM prototype requires Windows DPAPI and CNG.")
endif()

add_library(atm_core atm_logic.cpp account_store.cpp credential_hasher.cpp)
target_link_libraries(atm_core PUBLIC bcrypt crypt32)

add_executable(atm_simulator WIN32 main.cpp)
target_link_libraries(atm_simulator PRIVATE atm_core)
