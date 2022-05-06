LLVM_VERSION = 13.0.0

CUR_DIR = $(shell pwd)

LLVM_DIR = ${CUR_DIR}/../llvm/llvm-${LLVM_VERSION}

SRC_DIR = ${CUR_DIR}/src
BUILD_DIR = ${CUR_DIR}/build

NPROC = $(shell nproc)

build_func = \
    (mkdir -p ${2} \
     && cd ${2} \
     && PATH=${LLVM_DIR}/bin:${PATH} \
        LLVM_ROOT_DIR=${LLVM_DIR}/bin \
        LLVM_LIBRARY_DIRS=${LLVM_DIR}/lib \
        LLVM_INCLUDE_DIRS=${LLVM_DIR}/include \
        cmake ${1} -DCMAKE_BUILD_TYPE=Release \
                   -DCMAKE_CXX_FLAGS_RELEASE="-std=c++14 -fno-rtti -fpic -g" \
     && make -j${NPROC})

all: mpirace

mpirace:
	$(call build_func, ${SRC_DIR}, ${BUILD_DIR})

clean:
	rm -rf ${BUILD_DIR}
