# Signs the installed application bundle.
#
# The Qt deployment rewrites the frameworks and the plugins it copies, so that
# they find each other inside the bundle rather than in the folder Qt was
# installed in. Rewriting a file breaks the signature it carries, and macOS
# refuses to load a library whose signature no longer matches. The application
# is then killed before it opens a window, with a signature error and no
# message of its own.
#
# The bundle is signed again here, with an ad hoc signature, which is enough
# for it to run on the machine that built it. A bundle that travels to
# another machine needs a signature of its own, which build-macos.md
# describes.
#
# A signature that cannot be made is reported, but it does not stop the
# install: the bundle is still worth having without one, and a host that
# forbids the operation is not the host the bundle is built for.
#
# The bundle to sign is passed in as the variable "bundle".

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED bundle)
    message(FATAL_ERROR "SignBundle.cmake: the bundle to sign is not set")
endif()

if(NOT EXISTS "${bundle}")
    message(FATAL_ERROR "SignBundle.cmake: the bundle to sign is not there: ${bundle}")
endif()

find_program(codesign_tool codesign)
if(NOT codesign_tool)
    message(WARNING "SignBundle.cmake: codesign was not found, the bundle stays as it is")
    return()
endif()

# The signature of every file in the bundle is checked when it is loaded, so
# each of them is signed on its own. The ones in the plugin folder are plain
# libraries rather than bundles of their own, which the deep signing of the
# bundle does not always reach.
file(GLOB_RECURSE bundle_code
    "${bundle}/Contents/Frameworks/*"
    "${bundle}/Contents/PlugIns/*"
    "${bundle}/Contents/lib/*"
    "${bundle}/Contents/MacOS/*"
)

set(signed_count 0)
foreach(code_file IN LISTS bundle_code)
    if(IS_DIRECTORY "${code_file}" OR IS_SYMLINK "${code_file}")
        continue()
    endif()

    # A file that a signing run left behind is not part of the bundle.
    if(code_file MATCHES "\\.cstemp$")
        continue()
    endif()

    # Only a Mach-O file carries a signature. The magic number at its start
    # tells the kinds apart: the plain ones, the 64 bit ones and the ones
    # that hold several architectures. The numbers are read as they sit in the
    # file, so the little endian ones are the ones whose bytes are reversed.
    #
    # The comparison is made on lower case letters, since the hex reader
    # writes them that way and a match is exact about the case.
    file(READ "${code_file}" magic LIMIT 4 HEX)
    string(TOLOWER "${magic}" magic)
    if(NOT magic MATCHES "^(feedface|cefaedfe|feedfacf|cffaedfe|cafebabe|bebafeca)")
        continue()
    endif()

    execute_process(
        COMMAND "${codesign_tool}" --force --sign - "${code_file}"
        RESULT_VARIABLE sign_result
        ERROR_VARIABLE sign_error
    )
    if(sign_result EQUAL 0)
        math(EXPR signed_count "${signed_count} + 1")
    else()
        message(WARNING "SignBundle.cmake: ${code_file} could not be signed: ${sign_error}")
    endif()
endforeach()

# The bundle itself is signed last, so that the resources of the application
# are bound to the signature of its executable.
execute_process(
    COMMAND "${codesign_tool}" --force --deep --sign - "${bundle}"
    RESULT_VARIABLE sign_result
    ERROR_VARIABLE sign_error
)
if(sign_result EQUAL 0)
    message(STATUS "Signed ${bundle}, with ${signed_count} libraries of its own")
else()
    message(WARNING "SignBundle.cmake: the bundle could not be signed: ${sign_error}")
endif()
