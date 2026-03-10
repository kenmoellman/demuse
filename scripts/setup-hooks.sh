#!/bin/bash
# setup-hooks.sh - Install git hooks for this repository
#
# Run once after a fresh clone:
#   scripts/setup-hooks.sh

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$REPO_ROOT" ]; then
    echo "Error: not in a git repository" >&2
    exit 1
fi

HOOKS_DIR="$REPO_ROOT/.git/hooks"

# Install pre-commit hook
cat > "$HOOKS_DIR/pre-commit" << 'EOF'
#!/bin/bash
# pre-commit hook - auto-update version number in credits.h
# See scripts/update-version.sh for details
#
# To install after a fresh clone:
#   scripts/setup-hooks.sh

REPO_ROOT="$(git rev-parse --show-toplevel)"
"$REPO_ROOT/scripts/update-version.sh"
EOF

chmod +x "$HOOKS_DIR/pre-commit"
echo "Installed pre-commit hook (auto-version in credits.h)"
