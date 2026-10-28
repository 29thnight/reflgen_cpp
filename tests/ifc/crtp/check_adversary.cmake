execute_process(COMMAND "${GENERATOR}" --ifc "${IFC}" --import "${IMPORT_NAME}"
    --module crtp_adversary --module-interface "${INTERFACE_NAME}" --output "${OUTPUT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(result EQUAL 0 OR NOT err MATCHES "unsupported inheritance")
    message(FATAL_ERROR "CRTP partial specialization must fail closed: ${result}\n${out}\n${err}")
endif()
if(EXISTS "${OUTPUT}/${INTERFACE_NAME}.ixx")
    message(FATAL_ERROR "A rejected inheritance must not publish a reflection module")
endif()
