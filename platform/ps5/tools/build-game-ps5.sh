#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Native development player only; never copy private user data into this title.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
upstream=${ORQENVA_VULKAN_ROOT:?Set ORQENVA_VULKAN_ROOT to the pinned independent PS5 Vulkan source/build}
sdk=$upstream/.deps/native/ps5-payload-sdk
archive=$upstream/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a
tool=$upstream/build/host/ps5-native-tool
work=$root/build-game-ps5
app=$root/dist/PPSA99429
[[ $(cat "$sdk/.ps5-sdk-revision") == b83202be73e930050e00de0f1b4d9ec46c0391bf ]]
[[ $(sha256sum "$archive" | cut -d ' ' -f 1) == e70481a732a468cb6145347d8815ea06abd6a00e6e1978b7f9ab190aa17b75fe ]]
mkdir -p "$work/obj" "$work/gen" "$work/stubs" "$app/sce_sys" "$app/sce_module"
export PS5_PAYLOAD_SDK=$sdk
export PS5_CLANG=${PS5_CLANG:-/usr/lib/llvm-18/bin/clang}
glslang=${GLSLANG:-glslangValidator}
: > "$work/gen/game_shaders.h"
for name in tiles geometry geometry_resolve; do
 "$glslang" -V --target-env vulkan1.1 --vn "oq_$name" "$root/runtime/shaders/$name.comp" -o "$work/gen/$name.h" > /dev/null
 cat "$work/gen/$name.h" >> "$work/gen/game_shaders.h"
done
compile() {
 local source=$1 destination=$2 language=$3
 sh "$root/tools/cc-init-array.sh" sh "$root/tools/prospero-clang18" "-std=$language" -O2 -g1 \
  -ffunction-sections -fdata-sections -DOQ_PS5 -I "$root/runtime" -I "$root/src" -I "$work/gen" -I "$root/../../runtime/include" \
  -I "$upstream/.deps/native/radv-release/include" -fno-exceptions -fno-rtti -c "$source" -o "$destination"
}
objects=()
for source in runtime/game_player.cpp runtime/game_module.cpp runtime/vulkan_tiles.cpp runtime/ps5_files.c src/display.cpp \
 src/platform.c src/shims.c tooling/native/app_crt.cpp tooling/native/app_cpp_runtime.cpp; do
 object=$work/obj/$(basename "$source").o;language=c++17
 [[ $source == *.c ]] && language=c11
 compile "$root/$source" "$object" "$language";objects+=("$object")
done
for name in agc agc_driver; do
 compile "$root/tooling/native/${name}_link_stub.c" "$work/obj/$name-stub.o" c11
done
"$sdk/bin/prospero-lld" --shared -soname libSceAgc.prx -o "$work/stubs/libSceAgc.so" "$work/obj/agc-stub.o"
"$sdk/bin/prospero-lld" --shared -soname libSceAgcDriver.prx -o "$work/stubs/libSceAgcDriver.so" "$work/obj/agc_driver-stub.o"
source "$root/tools/platform-link.sh"
radv_link_recipe "$root" "$sdk" "$archive"
names=(isatty getresuid getresgid dl_iterate_phdr fchown fork vfork setsid pathconf getnameinfo gai_strerror lstat)
printf '{ local:\n' > "$work/shims.map"
for name in "${names[@]}";do radv_link_flags+=("--defsym=$name=ps5dl_$name");printf '%s;\n' "$name" >> "$work/shims.map";done
for name in mkstemp readlink;do radv_link_flags+=("--defsym=$name=oq_$name");printf '%s;\n' "$name" >> "$work/shims.map";done
printf '};\n' >> "$work/shims.map"
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --no-dynamic-linker --eh-frame-hdr "${radv_link_flags[@]}" \
 --version-script "$work/shims.map" --version-script "$root/tooling/native/app-symbols.map" --exclude-libs=ALL \
 -e _start -o "$work/title.elf" "${objects[@]}" "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
 "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
"$tool" link --in "$work/title.elf" --out "$work/eboot.elf" --stub-dir "$sdk/target/lib" \
 --stub "$work/stubs/libSceAgc.so" --stub "$work/stubs/libSceAgcDriver.so" \
 --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > "$work/self-inspection.txt"
(cd "$upstream/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$upstream/runtime/libc.prx" "$app/sce_module/"
cp "$root/../../sce_sys/icon0.png" "$app/sce_sys/"
mkdir -p "$app/licenses"
cp "$root/../../assets/controller/NOTICE.md" "$app/licenses/controller-art-NOTICE.md"
"${PYTHON:-python3}" "$root/../../assets/prepare-ui.py" "$app"
python3 - "$root" "$app" <<'PY'
import json,sys
from pathlib import Path
root,app=map(Path,sys.argv[1:]);p=json.loads((root/'../../sce_sys/param.json').read_text())
p.update(titleId='PPSA99429',conceptId='99429',contentId='UP9000-PPSA99429_00-APRICOTGSREADER00',contentVersion='00.005.009',targetContentVersion='00.005.009')
p.setdefault('pubtools',{})['loudnessSnd0']='-28.00'
for record in p['localizedParameters'].values():
 if isinstance(record,dict):record['titleName']='ApricotGS'
(app/'sce_sys/param.json').write_text(json.dumps(p,indent=2)+'\n')
print('PS5_PLAYER_BUILD',app,'ApricotGS 0.0.2; no game data included; this build is not installed automatically')
PY
