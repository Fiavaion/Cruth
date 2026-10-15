# Runs the phase-0 acceptance chain (tools/cruth-acceptance) at smoke scale and
# decodes its PNG/EXR outputs independently when Python is available.
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
execute_process(COMMAND ${ACCEPTANCE} --scale smoke --out ${WORK}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "acceptance chain failed (${rc}); see ${WORK}/report.json")
endif()
if(PYTHON)
  file(GLOB maps ${WORK}/*.exr ${WORK}/*.png)
  execute_process(COMMAND ${PYTHON} ${CHECKER} ${maps} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message(STATUS "${out}${err}")
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "image check failed (${rc})")
  endif()
endif()
