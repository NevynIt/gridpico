file(MAKE_DIRECTORY "${asset_dir}")
configure_file("${source_dir}/web/apc/index.html" "${asset_dir}/INDEX.HTM" COPYONLY)
configure_file("${source_dir}/connections/diagnostics/index.html" "${asset_dir}/DIAG.HTM" COPYONLY)

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

file(SHA256 "${asset_dir}/INDEX.HTM" apc_hash)
file(SHA256 "${asset_dir}/DIAG.HTM" diagnostics_hash)
file(WRITE "${asset_dir}/VERSION.TXT"
    "gridpico-assets 1\ncheckout-base ${revision}\n"
    "sha256 INDEX.HTM ${apc_hash}\n"
    "sha256 DIAG.HTM ${diagnostics_hash}\n"
)
