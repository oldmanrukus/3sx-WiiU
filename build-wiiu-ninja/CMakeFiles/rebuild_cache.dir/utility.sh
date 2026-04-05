set -e

cd /home/billy/3sx/build-wiiu-ninja
/usr/bin/cmake.exe --regenerate-during-build -S$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
