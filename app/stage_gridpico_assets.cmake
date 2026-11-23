file(MAKE_DIRECTORY "${asset_dir}")
file(READ "${source_dir}/connections/home.html" home)
foreach(link IN ITEMS /apc/ /diagnostics/ /fm/)
    string(FIND "${home}" "href=\"${link}\"" link_position)
    if(link_position EQUAL -1)
        message(FATAL_ERROR "Product home is missing its ${link} link")
    endif()
endforeach()
string(REPLACE "href=\"/apc/\"" "href=\"INDEX.HTM\"" home "${home}")
string(REPLACE "href=\"/diagnostics/\"" "href=\"DIAG.HTM\"" home "${home}")
string(REPLACE "href=\"/fm/\"" "href=\"FM.HTM\"" home "${home}")
file(WRITE "${asset_dir}/HOME.HTM" "${home}")

function(inline_script path output)
    file(READ "${source_dir}/${path}" script)
    string(TOLOWER "${script}" script_lower)
    if(script_lower MATCHES "</script")
        message(FATAL_ERROR "Cannot inline ${path}: it contains a closing script tag")
    endif()
    set("${output}" "<script>\n${script}</script>\n" PARENT_SCOPE)
endfunction()

function(stage_tool path output scripts)
    file(READ "${source_dir}/${path}" html)
    if(NOT html MATCHES "</head>")
        message(FATAL_ERROR "Cannot inline browser MIDI into ${path}: missing closing head tag")
    endif()
    string(REPLACE "</head>" "${scripts}</head>" html "${html}")
    file(WRITE "${asset_dir}/${output}" "${html}")
endfunction()

inline_script("connections/local-transport.js" midi)
inline_script("connections/local-diagnostics.js" diagnostics)
stage_tool("web/apc/index.html" "INDEX.HTM" "${midi}")
stage_tool("connections/diagnostics/index.html" "DIAG.HTM" "${midi}${diagnostics}")
stage_tool("web/fm/index.html" "FM.HTM" "${midi}")

execute_process(
    COMMAND git rev-parse --verify HEAD
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE git_result
    OUTPUT_VARIABLE revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT git_result STREQUAL "0")
    set(revision "unknown")
endif()

file(SHA256 "${asset_dir}/HOME.HTM" home_hash)
file(SHA256 "${asset_dir}/INDEX.HTM" apc_hash)
file(SHA256 "${asset_dir}/DIAG.HTM" diagnostics_hash)
file(SHA256 "${asset_dir}/FM.HTM" fm_hash)
file(WRITE "${asset_dir}/VERSION.TXT"
    "gridpico-assets 1\ncheckout-base ${revision}\n"
    "sha256 HOME.HTM ${home_hash}\n"
    "sha256 INDEX.HTM ${apc_hash}\n"
    "sha256 DIAG.HTM ${diagnostics_hash}\n"
    "sha256 FM.HTM ${fm_hash}\n"
)
