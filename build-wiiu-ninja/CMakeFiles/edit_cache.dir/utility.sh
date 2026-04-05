set -e

cd /home/billy/3sx/build-wiiu-ninja
/usr/bin/ccmake.exe -S$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
