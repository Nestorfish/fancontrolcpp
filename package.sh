#!/bin/sh
version="$1"
[ -z "$version" ] && { echo "Give version" >&2; exit 1; }
base_directory="../packaging"
prog="fancontrolcpp"
progver="${prog}-${version}"

[ -e "${base_directory}/${progver}" ] && { echo "${base_directory}/${progver} already exists" >&2; exit 1; }

mkdir "${base_directory}/${progver}"
cp *cpp "${base_directory}/${progver}"
cp -r lib/ "${base_directory}/${progver}"

cd "${base_directory}"
tar -cJf "${progver}.tar.xz" "$progver"

cd "$progver"
dh_make -f "../${progver}.tar.xz" --single --yes
sed -i 's/^Section: unknown$/Section: utils/' debian/control
