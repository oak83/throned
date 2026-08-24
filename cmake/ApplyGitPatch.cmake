foreach(required_variable GIT_EXECUTABLE PATCH_SOURCE_DIR PATCH_FILE)
    if (NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif ()
endforeach ()

execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
        WORKING_DIRECTORY "${PATCH_SOURCE_DIR}"
        RESULT_VARIABLE patch_check_result
        ERROR_VARIABLE patch_check_error
)

if (patch_check_result EQUAL 0)
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH_FILE}"
            WORKING_DIRECTORY "${PATCH_SOURCE_DIR}"
            RESULT_VARIABLE patch_result
            ERROR_VARIABLE patch_error
    )
    if (NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}:\n${patch_error}")
    endif ()
    return()
endif ()

# FetchContent can revisit the patch step in an existing build tree. Treat an
# already-applied patch as success, while still failing loudly if the pinned
# dependency changes in a way that makes the patch ambiguous.
execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
        WORKING_DIRECTORY "${PATCH_SOURCE_DIR}"
        RESULT_VARIABLE reverse_check_result
        ERROR_VARIABLE reverse_check_error
)
if (NOT reverse_check_result EQUAL 0)
    message(FATAL_ERROR
            "Patch ${PATCH_FILE} neither applies nor is already applied.\n"
            "Apply check: ${patch_check_error}\n"
            "Reverse check: ${reverse_check_error}")
endif ()
