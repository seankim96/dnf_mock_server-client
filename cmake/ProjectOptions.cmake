option(DNF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(DNF_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(DNF_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(DNF_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(DNF_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
option(DNF_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

function(dnf_configure_project_options)
    if(TARGET dnf_project_options)
        return()
    endif()

    if(DNF_ENABLE_TSAN AND (DNF_ENABLE_ASAN OR DNF_ENABLE_UBSAN))
        message(FATAL_ERROR
            "ThreadSanitizer cannot be combined with ASan or UBSan")
    endif()

    if(DNF_ENABLE_COVERAGE AND
       (DNF_ENABLE_ASAN OR DNF_ENABLE_UBSAN OR DNF_ENABLE_TSAN))
        message(FATAL_ERROR
            "Coverage instrumentation cannot be combined with sanitizers")
    endif()

    add_library(dnf_project_options INTERFACE)

    if(MSVC)
        target_compile_options(dnf_project_options INTERFACE
            /W4
            /permissive-
            /EHsc
        )

        if(DNF_WARNINGS_AS_ERRORS)
            target_compile_options(dnf_project_options INTERFACE /WX)
        endif()

        if(DNF_ENABLE_ASAN)
            target_compile_options(dnf_project_options INTERFACE /fsanitize=address)
        endif()

        if(DNF_ENABLE_UBSAN OR DNF_ENABLE_TSAN OR DNF_ENABLE_COVERAGE)
            message(FATAL_ERROR
                "The selected sanitizer or coverage option is not supported by MSVC")
        endif()
    else()
        target_compile_options(dnf_project_options INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-missing-field-initializers
        )

        if(DNF_WARNINGS_AS_ERRORS)
            target_compile_options(dnf_project_options INTERFACE -Werror)
        endif()

        set(sanitizers "")
        if(DNF_ENABLE_ASAN)
            list(APPEND sanitizers address)
        endif()
        if(DNF_ENABLE_UBSAN)
            list(APPEND sanitizers undefined)
        endif()
        if(DNF_ENABLE_TSAN)
            list(APPEND sanitizers thread)
        endif()

        if(sanitizers)
            list(JOIN sanitizers "," sanitizer_flags)
            target_compile_options(dnf_project_options INTERFACE
                -fsanitize=${sanitizer_flags}
                -fno-omit-frame-pointer
            )
            target_link_options(dnf_project_options INTERFACE
                -fsanitize=${sanitizer_flags}
                -fno-omit-frame-pointer
            )
        endif()

        if(DNF_ENABLE_COVERAGE)
            target_compile_options(dnf_project_options INTERFACE
                --coverage
                -O0
                -g
            )
            target_link_options(dnf_project_options INTERFACE --coverage)
        endif()
    endif()

    if(DNF_ENABLE_CLANG_TIDY)
        find_program(DNF_CLANG_TIDY_COMMAND NAMES clang-tidy REQUIRED)
        set(CMAKE_CXX_CLANG_TIDY
            "${DNF_CLANG_TIDY_COMMAND};--warnings-as-errors=clang-analyzer-*"
            PARENT_SCOPE)
    endif()
endfunction()
