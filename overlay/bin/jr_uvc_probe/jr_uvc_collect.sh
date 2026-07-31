#!/bin/bash
# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

set -u
set -o pipefail

if [ "$#" -gt 2 ]; then
	echo "usage: jr_uvc_collect [OUTPUT_ROOT] [FRAMES]" >&2
	exit 2
fi

output_root=${1:-/boot/home}
frames=${2:-3}

case "$frames" in
	''|*[!0-9]*|0)
		echo "jr_uvc_collect: FRAMES must be a positive integer" >&2
		exit 2
		;;
esac

run_name=jr_uvc-run-$(date +%Y%m%d-%H%M%S)
run_dir=$output_root/$run_name
artifact_dir=$run_dir/artifacts
console_path=$run_dir/console.txt
exit_path=$run_dir/exit.txt
archive_path=$output_root/$run_name.zip
diagnostics_path=$run_dir/diagnostics.txt

snapshot_syslog()
{
	local phase=$1
	local source
	for source in /boot/system/var/log/syslog /var/log/syslog; do
		if [ -r "$source" ]; then
			if cp "$source" "$run_dir/syslog-$phase.txt"; then
				printf 'syslog_%s=%s\n' "$phase" "$source" \
					>> "$diagnostics_path"
			else
				printf 'syslog_%s=copy_failed:%s\n' "$phase" "$source" \
					>> "$diagnostics_path"
			fi
			return
		fi
	done
	printf 'syslog_%s=unavailable\n' "$phase" >> "$diagnostics_path"
}

snapshot_usb()
{
	local phase=$1
	local listusb_status
	if command -v listusb >/dev/null 2>&1; then
		listusb > "$run_dir/listusb-$phase.txt" 2>&1
		listusb_status=$?
		printf 'listusb_%s_status=%s\n' "$phase" "$listusb_status" \
			>> "$diagnostics_path"
	else
		printf 'listusb_%s_status=unavailable\n' "$phase" \
			>> "$diagnostics_path"
	fi
}

mkdir -p "$artifact_dir" || {
	echo "jr_uvc_collect: cannot create $artifact_dir" >&2
	exit 1
}
: > "$diagnostics_path"

echo "jr_uvc_collect: output $run_dir"
echo "jr_uvc_collect: running $frames frame(s) per mode"

snapshot_syslog before
snapshot_usb before

jr_uvc_probe --output "$artifact_dir" --frames "$frames" 2>&1 \
	| tee "$console_path"
probe_status=${PIPESTATUS[0]}
printf '%s\n' "$probe_status" > "$exit_path"

sleep 1
snapshot_usb after
snapshot_syslog after

if command -v zip >/dev/null 2>&1; then
	(
		cd "$output_root" || exit 1
		rm -f "$archive_path"
		zip -rq "$archive_path" "$run_name"
	)
	zip_status=$?
	if [ "$zip_status" -eq 0 ]; then
		echo "jr_uvc_collect: archive $archive_path"
	else
		echo "jr_uvc_collect: zip failed; directory retained at $run_dir" >&2
	fi
else
	echo "jr_uvc_collect: zip unavailable; directory retained at $run_dir" >&2
fi

exit "$probe_status"
