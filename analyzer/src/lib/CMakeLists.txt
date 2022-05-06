set (MPIRaceSource
    common.h
    common.cc
    mpicall.h
    mpicall.cc
    mpirace.h
    mpirace.cc
    global.h
    main.cc
)

add_library(MPIRaceObj OBJECT ${MPIRaceSource})
add_library(mpirace-shared SHARED $<TARGET_OBJECTS:MPIRaceObj>)
add_library(mpirace-static STATIC $<TARGET_OBJECTS:MPIRaceObj>)

set(EXECUTABLE_OUTPUT_PATH ${MPIRace_BINARY_DIR})

link_directories(${MPIRace_BINARY_DIR}/lib)
add_executable(mpirace ${MPIRaceSource})
target_link_libraries(mpirace
    LLVMAsmParser
    LLVMSupport
    LLVMCore
    LLVMAnalysis
    LLVMIRReader
    mpirace-static
)
