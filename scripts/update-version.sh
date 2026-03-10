#!/bin/bash
# update-version.sh - Auto-generate version number in credits.h
#
# Version format: 2.YY.MM.DD beta X
#   2    = major version (always 2)
#   YY   = 2-digit year
#   MM   = month (1-12, no leading zero)
#   DD   = day (1-31, no leading zero)
#   X    = commit number for that day (previous commits today + 1)
#
# Called by the pre-commit hook. Can also be run manually.

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$REPO_ROOT" ]; then
    echo "Error: not in a git repository" >&2
    exit 1
fi

CREDITS="$REPO_ROOT/src/hdrs/credits.h"
if [ ! -f "$CREDITS" ]; then
    echo "Error: credits.h not found at $CREDITS" >&2
    exit 1
fi

# Get date components
YY=$(date +%y)
MM=$(date +%-m)
DD=$(date +%-d)

# Count commits already made today (this commit will be the next one)
TODAY=$(date +%Y-%m-%d)
COUNT=$(git log --oneline --after="${TODAY}T00:00:00" --before="${TODAY}T23:59:59" 2>/dev/null | wc -l)
BETA=$((COUNT + 1))

VERSION="2.${YY}.${MM}.${DD} beta ${BETA}"

# Update the BASE_VERSION line in credits.h
sed -i "s@^#define BASE_VERSION.*@#define BASE_VERSION\t\"|W+DeMUSE ${VERSION}|\"@" "$CREDITS"

# Stage the updated file so it's included in the commit
git add "$CREDITS"

echo "Version updated to: ${VERSION}"
