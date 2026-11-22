cmake_minimum_required(VERSION 3.16)
project(mini_shell_c C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=undefined)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(minishell_core STATIC
  src/tokenizer.c
  src/parser.c
  src/jobs.c
  src/signals.c
  src/executor.c
  src/builtins.c
)

add_executable(minishell src/main.c)
target_link_libraries(minishell PRIVATE minishell_core)

enable_testing()
add_subdirectory(tests/c)
