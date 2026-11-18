# The Neura tools that `neura` starts from its own directory
# (docs/design/Products030.md). A test that copies `neura` somewhere copies
# them with it: a lone dispatcher reports each missing tool instead of
# running the command.
set(NEURA_TOOL_NAMES
    neura-run neura-inspect neura-doctor neura-update neura-uninstall neura-server-host
    neurac neura-test neura-doc neura-cbind neura-decompile neura-asm neura-disasm
    neura-explain neura-tags neura-debug neura-lsp neura-fmt)

# neura_copy_tools(<neura> <directory>): copies <neura>, every tool next to it
# and their shared libraries into <directory>.
function(neura_copy_tools neura directory)
    get_filename_component(source "${neura}" DIRECTORY)
    get_filename_component(suffix "${neura}" EXT)
    set(files "${neura}")
    foreach(name IN LISTS NEURA_TOOL_NAMES)
        if(EXISTS "${source}/${name}${suffix}")
            list(APPEND files "${source}/${name}${suffix}")
        endif()
    endforeach()
    file(COPY ${files} DESTINATION "${directory}")
    # The shared libraries of the tools, which a build-tree tool finds in
    # lib/neura beside itself.
    file(GLOB libraries "${source}/lib/neura/libneura-*")
    if(libraries)
        file(COPY ${libraries} DESTINATION "${directory}/lib/neura")
    endif()
endfunction()
