#!/bin/bash -e
set -o pipefail

KLEAF_REPO_DIR="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
"${KLEAF_REPO_DIR}/build/kernel/kleaf/workspace_status.sh" | sed -E 's/-dirty(["-])/\1/g'
