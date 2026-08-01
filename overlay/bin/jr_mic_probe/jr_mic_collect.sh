#!/bin/bash
# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

set -u
set -o pipefail

if [ "$#" -gt 2 ]; then
	echo "usage: jr_mic_collect [OUTPUT_ROOT] [SECONDS]" >&2
	exit 2
fi

output_root=${1:-/boot/home}
seconds=${2:-5}
case "$seconds" in
	''|*[!0-9]*)
		echo "jr_mic_collect: SECONDS must be an integer from 1 to 30" >&2
		exit 2
		;;
esac
if [ "$seconds" -lt 1 ] || [ "$seconds" -gt 30 ]; then
	echo "jr_mic_collect: SECONDS must be an integer from 1 to 30" >&2
	exit 2
fi

run_name=jr_mic-run-$(date +%Y%m%d-%H%M%S)
run_dir=$output_root/$run_name
archive_path=$output_root/$run_name.zip
mkdir -p "$run_dir" || exit 1

for source in /boot/system/var/log/syslog /var/log/syslog; do
	if [ -r "$source" ]; then
		cp "$source" "$run_dir/syslog-before.txt"
		syslog_source=$source
		break
	fi
done

echo "jr_mic_collect: output $run_dir"
jr_mic_probe "$run_dir/microphone.wav" "$seconds" 2>&1 \
	| tee "$run_dir/console.txt"
probe_status=${PIPESTATUS[0]}
printf '%s\n' "$probe_status" > "$run_dir/exit.txt"

sleep 1
if [ -n "${syslog_source:-}" ]; then
	cp "$syslog_source" "$run_dir/syslog-after.txt"
fi

if command -v zip >/dev/null 2>&1; then
	(
		cd "$output_root" || exit 1
		rm -f "$archive_path"
		zip -rq "$archive_path" "$run_name"
	)
	if [ "$?" -eq 0 ]; then
		echo "jr_mic_collect: archive $archive_path"
	else
		echo "jr_mic_collect: zip failed; directory retained at $run_dir" >&2
	fi
fi

exit "$probe_status"
