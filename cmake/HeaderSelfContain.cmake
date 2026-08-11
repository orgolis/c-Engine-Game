# =============================================================================
# HeaderSelfContain — every public header must compile on its own.
#
# A header that uses uint32_t without including <cstdint> compiles perfectly in
# a full build, because some other header in the same translation unit already
# pulled the type in. It then fails in CI, or in a user's project, or in the
# next file that happens to include it first. Include order is not a guarantee,
# and a green local build can be green by accident.
#
# This broke the v0.6.0 release: command_palette.h used uint32_t with no
# <cstdint>, built here, and failed on the CI runner. A full build cannot catch
# it -- only compiling the header ALONE can.
#
# gws_add_header_selfcontain_check(<name> TARGET <t> HEADERS <h>... [EXCLUDE <h>...])
#
# Generates one translation unit per header that includes it and nothing else,
# and compiles them with <t>'s include directories. Any header that needs a
# companion include it does not name fails the build.
# =============================================================================

function(gws_add_header_selfcontain_check NAME)
    cmake_parse_arguments(A "" "TARGET" "HEADERS;EXCLUDE" ${ARGN})
    if(NOT A_TARGET)
        message(FATAL_ERROR "gws_add_header_selfcontain_check(${NAME}): TARGET is required")
    endif()

    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_selfcontain")
    file(MAKE_DIRECTORY "${gen_dir}")

    set(sources "")
    foreach(hdr ${A_HEADERS})
        get_filename_component(hdr_abs "${hdr}" ABSOLUTE)
        get_filename_component(hdr_name "${hdr}" NAME)

        set(skip FALSE)
        foreach(ex ${A_EXCLUDE})
            if(hdr_name STREQUAL ex)
                set(skip TRUE)
            endif()
        endforeach()
        if(skip)
            continue()
        endif()

        string(MAKE_C_IDENTIFIER "${hdr_name}" stem)
        set(tu "${gen_dir}/tu_${stem}.cpp")
        # The header twice: the second include also proves the include guard
        # works, which is the other way a header quietly breaks a build.
        file(WRITE "${tu}"
             "#include \"${hdr_abs}\"\n"
             "#include \"${hdr_abs}\"\n")
        list(APPEND sources "${tu}")
    endforeach()

    if(NOT sources)
        return()
    endif()

    add_library(${NAME} OBJECT ${sources})
    target_link_libraries(${NAME} PRIVATE ${A_TARGET})
    target_compile_features(${NAME} PRIVATE cxx_std_20)
    set_target_properties(${NAME} PROPERTIES EXCLUDE_FROM_ALL FALSE)

    list(LENGTH sources n)
    message(STATUS "header self-containment: ${n} headers checked in ${NAME}")
endfunction()
