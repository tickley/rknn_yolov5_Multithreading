set -e

TARGET_SOC="RK3566_RK3568"
#GCC_COMPILER=aarch64-linux-gnu
GCC_COMPILER=/home/heng/rk356x_1.4.0_20231220/buildroot/output/rockchip_rk3568/host/bin/aarch64-buildroot-linux-gnu

export LD_LIBRARY_PATH=${TOOL_CHAIN}/lib64:$LD_LIBRARY_PATH
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake ../.. -DCMAKE_SYSTEM_NAME=Linux -DTARGET_SOC=${TARGET_SOC}
make -j4
make install
cd -
