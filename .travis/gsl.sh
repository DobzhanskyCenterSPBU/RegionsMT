declare -a conf=(Debug Release)
for c in ${conf[*]}; do
    rm -rf ./gsl-${c}
    mkdir ./gsl-${c}
    cd ./gsl-${c}
    cmake -D CMAKE_C_FLAGS="${CMAKE_C_FLAGS} -flto -mavx" -D CMAKE_EXE_LINKER_FLAGS="${CMAKE_EXE_LINKER_FLAGS} -flto" -D CMAKE_AR="/usr/bin/gcc-ar" -D CMAKE_C_ARCHIVE_CREATE="<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>" -D CMAKE_C_ARCHIVE_FINISH=true -D CMAKE_BUILD_TYPE=${c} ../gsl
    make
    cd ..
done
