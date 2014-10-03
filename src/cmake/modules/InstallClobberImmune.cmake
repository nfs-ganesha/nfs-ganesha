# Determines at `make install` time if a file, typically a configuration
# file placed in $PREFIX/etc, shouldn't be installed to prevent overwrite
# of an existing file.
#
# _srcfile: the file to install
# _dstfile: the absolute file name after installation

macro(InstallClobberImmune _srcfile _dstfile)
    install(CODE "
        set(_destfile \"${_dstfile}\")
        if (NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
            # prepend install root prefix with install-time DESTDIR
            set(_destfile \"\$ENV{DESTDIR}/${_dstfile}\")
        endif ()
        if (EXISTS \${_destfile})
            message(STATUS \"Skipping: \${_destfile} (already exists)\")
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E compare_files
                ${_srcfile} \${_destfile} RESULT_VARIABLE _diff)
            if (NOT \"\${_diff}\" STREQUAL \"0\")
                message(STATUS \"Installing: \${_destfile}.example\")
                configure_file(${_srcfile} \${_destfile}.example COPYONLY)
            endif ()
        else ()
            message(STATUS \"Installing: \${_destfile}\")
            # install() is not scriptable within install(), and
            # configure_file() is the next best thing
            configure_file(${_srcfile} \${_destfile} COPYONLY)
            # TODO: create additional install_manifest files?
        endif ()
    ")
endmacro(InstallClobberImmune)
