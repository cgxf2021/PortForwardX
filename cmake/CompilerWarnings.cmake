function(portforwardx_apply_warnings target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "portforwardx_apply_warnings: not a target: ${target}")
  endif()

  set(warnings "")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    list(APPEND warnings
      -Wall -Wextra -Wpedantic
      -Wshadow -Wnon-virtual-dtor -Wold-style-cast
      -Wcast-align -Wunused -Woverloaded-virtual
      -Wconversion -Wsign-conversion
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND warnings
      -Wall -Wextra -Wpedantic
      -Wshadow -Wnon-virtual-dtor -Wold-style-cast
      -Wcast-align -Wunused -Woverloaded-virtual
      -Wconversion -Wsign-conversion
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(APPEND warnings
      /W4
      /permissive-
    )
  endif()

  if(PORTFORWARDX_ENABLE_WARNINGS_AS_ERRORS)
    if(MSVC)
      list(APPEND warnings /WX)
    else()
      list(APPEND warnings -Werror)
    endif()
  endif()

  if(warnings)
    target_compile_options(${target} PRIVATE ${warnings})
  endif()
endfunction()
