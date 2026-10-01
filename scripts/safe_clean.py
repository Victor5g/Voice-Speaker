#!/usr/bin/python3
"""Fixed allowlist only. realpath-check ALL targets before deleting any."""
import argparse
import os
from pathlib import Path
import shutil

ROOT = Path(os.path.realpath(Path(__file__).parent.parent))
BUILD_TARGETS = ('build', '.build')
ALL_TARGETS = BUILD_TARGETS + ('cache', 'artifacts', 'tools/downloads', 'models/optional')

def checked_target(root, relative):
    root = Path(os.path.realpath(root))
    lexical = root / relative
    resolved = Path(os.path.realpath(lexical))
    if resolved == root or root not in resolved.parents:
        raise ValueError('ABORT: target is not strictly inside repository: ' + str(lexical))
    if lexical.is_symlink():
        raise ValueError('ABORT: cleanup target is a symlink: ' + str(lexical))
    return resolved

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not (ROOT / 'docs/REQUIREMENTS.md').is_file() or not (ROOT / 'scripts/safe_clean.py').is_file():
        raise SystemExit('ABORT: repository identity check failed')
    try:
        targets = [checked_target(ROOT, p) for p in (ALL_TARGETS if args.all else BUILD_TARGETS)]
    except ValueError as error:
        raise SystemExit(str(error))
    for target in targets:
        print(('WOULD REMOVE ' if args.dry_run else 'REMOVE ') + str(target))
        if not args.dry_run and target.exists():
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()

if __name__ == '__main__':
    main()
