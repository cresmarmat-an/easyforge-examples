cmake_minimum_required(VERSION 3.22...4.4)

project(easyforge-examples LANGUAGES CXX)

# easyforge is fetched the same way any program would fetch it. To build against
# a copy on this computer instead, configure with
#     -D FETCHCONTENT_SOURCE_DIR_EASYFORGE=../easyforge
include(FetchContent)
FetchContent_Declare(easyforge
    GIT_REPOSITORY https://github.com/cresmarmat-an/easyforge.git
    GIT_TAG main
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(easyforge)

# Adds the example in `folder` when every easyforge library it uses has been
# built. Examples for libraries that do not exist yet are skipped with a message,
# so the repository builds at every stage of easyforge.
#
#     add_example(01-empty-window LIBRARIES window)
function(add_example folder)
    cmake_parse_arguments(PARSE_ARGV 1 EXAMPLE "" "" "LIBRARIES")

    set(missing "")
    set(targets "")
    foreach(library IN LISTS EXAMPLE_LIBRARIES)
        if(TARGET easyforge::${library})
            list(APPEND targets easyforge::${library})
        else()
            list(APPEND missing ${library})
        endif()
    endforeach()

    if(missing)
        list(JOIN missing ", " missing_text)
        message(STATUS "Skipping ${folder}: easyforge does not have ${missing_text} yet")
        return()
    endif()

    file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp")
    add_executable(${folder} ${sources})
    target_link_libraries(${folder} PRIVATE ${targets})

    # Files next to main.cpp, such as images and sounds, are copied beside the program.
    file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*")
    list(FILTER files EXCLUDE REGEX "\\.(cpp|h)$")
    if(files)
        add_custom_command(TARGET ${folder} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${files} "$<TARGET_FILE_DIR:${folder}>"
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

add_example(01-empty-window LIBRARIES window)
