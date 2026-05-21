#!/bin/bash
cd "C:\Users\kschi\Documents\GitHub\cosmo-bot.worktrees\agents-audio-driver-testing-guide"

# Check what needs to be committed
echo "=== Git Status ==="
git status --short

echo ""
echo "=== Recent Commits ==="
git log --oneline -10

echo ""
echo "=== Diff Summary ==="
git diff --cached --stat

echo ""
echo "=== Full Diff ==="
git diff --cached
