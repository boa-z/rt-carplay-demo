#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

cd "${1:?Usage: bash tools/build_d213_mfi.sh ISOLATED_LUBAN_LITE_DIR hardware|software}"
mode=${2:?Choose hardware or software}
target=d213-demo128-nand-carplay-aic8800d40
mapfile -t settings < <(python3 .ci/hardware_targets.py settings "$target")
profile="target/configs/${settings[0]}"
registry=.ci/hardware-targets.json
sdk=packages/third-party/rt-carplay
demo=application/rt-thread/rt-carplay-demo
test -f "$sdk/include/rtcp/mfi_software.h"
grep -q '^config RTCARPLAY_DEMO_MFI_SOFTWARE$' "$demo/Kconfig"
git diff HEAD --quiet -- "$profile" "$registry"

case "$mode" in
    hardware)
        software=n
        hardware=y
        ;;
    software)
        software=y
        hardware=n
        ;;
    *) printf 'Unknown MFi mode %s\n' "$mode" >&2; exit 2 ;;
esac

original=$(mktemp -d)
cp "$profile" "$original/profile"
cp "$registry" "$original/registry"
trap 'cp "$original/profile" "$profile"; cp "$original/registry" "$registry"; rm -r "$original"' EXIT
sed -E '/^(# )?CONFIG_(LPKG_RT_CARPLAY_MFI_SOFTWARE|RTCARPLAY_DEMO_MFI_(SOFTWARE|EXTERNAL)|RTCARPLAY_MFI|LPKG_RT_CARPLAY_LUBANLITE_MFI|RTCARPLAY_OTA_COMPAT)(=| is not set)/d' "$original/profile" > "$profile"
printf '\nCONFIG_LPKG_RT_CARPLAY_MFI_SOFTWARE=%s\nCONFIG_RTCARPLAY_DEMO_MFI_SOFTWARE=%s\nCONFIG_RTCARPLAY_MFI=%s\n' \
    "$software" "$software" "$hardware" >> "$profile"
compat=${settings[6]:?Target requires OTA compatibility identifier}
if [[ $mode == software ]]; then
    compat="${compat}-mfi-software"
    python3 - "$registry" "$target" "$compat" <<'PY'
import json
from pathlib import Path
import sys

registry, target, compat = sys.argv[1:]
path = Path(registry)
config = json.loads(path.read_text(encoding="utf-8"))
config[target]["ota"]["compat"] = compat
path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
PY
fi
printf 'CONFIG_RTCARPLAY_OTA_COMPAT="%s"\n' "$compat" >> "$profile"

bash .ci/build-firmware.sh "$target"
artifact="output/ci/$target"
config="$artifact/firmware.config"
grep -Fx "CONFIG_RTCARPLAY_OTA_COMPAT=\"$compat\"" "$config"
firmware="output/${settings[0]%_defconfig}/images/${settings[2]}.elf"
symbols="output/ci-logs/$target/mfi-symbols.txt"
"${settings[4]}nm" "$firmware" > "$symbols"
if [[ $mode == hardware ]]; then
    grep -qx CONFIG_RTCARPLAY_MFI=y "$config"
    grep -q ' T rtcp_mfi_lubanlite_start$' "$symbols"
    if grep -q '^CONFIG_RTCARPLAY_DEMO_MFI_SOFTWARE=y$' "$config"; then
        printf 'Hardware image selected the software provider\n' >&2
        exit 1
    fi
else
    grep -qx CONFIG_LPKG_RT_CARPLAY_MFI_SOFTWARE=y "$config"
    grep -qx CONFIG_RTCARPLAY_DEMO_MFI_SOFTWARE=y "$config"
    grep -q ' T rtcp_mfi_software_create$' "$symbols"
    if grep -q ' [Tt] rtcp_mfi_lubanlite_start$' "$symbols"; then
        printf 'Software image contains the coprocessor provider\n' >&2
        exit 1
    fi
fi
(cd "$artifact" && sha256sum -c SHA256SUMS)
