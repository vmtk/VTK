IF(NOT VTK_INSTALL_NO_RUNTIME)

  CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/vtkhybrid.tcl.in
    ${CMAKE_CURRENT_BINARY_DIR}/vtkhybrid.tcl
    @ONLY)

  INSTALL(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vtkhybrid.tcl
    DESTINATION ${VTK_TCL_INSTALL_DIR_CM24}/vtkhybrid
    COMPONENT RuntimeLibraries)
ENDIF(NOT VTK_INSTALL_NO_RUNTIME)
