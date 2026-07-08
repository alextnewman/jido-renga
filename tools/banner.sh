#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot

# Jidō Renga — 自動連歌 — splash banner
# "the poem that writes itself" · a vine climbing Haiku's bamboo
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$here/banner.txt" ]; then
  cat "$here/banner.txt"
else
  printf '\n  JIDŌ RENGA\n  自動連歌 · the poem that writes itself\n\n'
fi
