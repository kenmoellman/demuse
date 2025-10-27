#!/usr/bin/env python3

import os
import re
import sys
from pathlib import Path

def process_file(filepath):
    """Process a single file and replace memory calls."""
    print(f"Processing: {filepath}")
    
    with open(filepath, 'r') as f:
        content = f.read()
    
    original_content = content
    
    # Track changes
    changes = []
    
    # Replace FREE() with SAFE_FREE()
    new_content = re.sub(r'\bFREE\s*\(', 'SAFE_FREE(', content)
    if new_content != content:
        changes.append("FREE() -> SAFE_FREE()")
        content = new_content
    
    # Replace MALLOC() with SAFE_MALLOC()
    new_content = re.sub(r'\bMALLOC\s*\(', 'SAFE_MALLOC(', content)
    if new_content != content:
        changes.append("MALLOC() -> SAFE_MALLOC()")
        content = new_content
    
    # Replace free() with SAFE_FREE() but NOT if it's a function definition
    # Look for free( that's not preceded by "void " or "void*"
    lines = content.split('\n')
    new_lines = []
    for i, line in enumerate(lines):
        # Skip if this looks like a function definition
        if re.search(r'^\s*void\s+free\s*\(', line):
            new_lines.append(line)
            continue
        
        # Replace free( with SAFE_FREE(
        new_line = re.sub(r'\bfree\s*\(', 'SAFE_FREE(', line)
        if new_line != line:
            changes.append(f"Line {i+1}: free() -> SAFE_FREE()")
        new_lines.append(new_line)
    
    content = '\n'.join(new_lines)
    
    # Find remaining malloc() calls for manual review
    malloc_matches = re.finditer(r'\bmalloc\s*\(', content)
    malloc_lines = []
    for match in malloc_matches:
        # Find line number
        line_num = content[:match.start()].count('\n') + 1
        # Get the line content
        line_start = content.rfind('\n', 0, match.start()) + 1
        line_end = content.find('\n', match.start())
        if line_end == -1:
            line_end = len(content)
        line_content = content[line_start:line_end].strip()
        malloc_lines.append((line_num, line_content))
    
    # Write changes if any were made
    if content != original_content:
        # Create backup
        backup_path = filepath + '.bak'
        with open(backup_path, 'w') as f:
            f.write(original_content)
        
        # Write modified content
        with open(filepath, 'w') as f:
            f.write(content)
        
        print(f"  ✓ Modified (backup: {backup_path})")
        for change in set(changes):
            if not change.startswith("Line"):
                print(f"    - {change}")
    else:
        print(f"  - No changes needed")
    
    # Report malloc() calls that need manual review
    if malloc_lines:
        print(f"  ⚠ Found {len(malloc_lines)} malloc() call(s) needing manual review:")
        for line_num, line_content in malloc_lines:
            print(f"    Line {line_num}: {line_content[:80]}")
    
    return len(malloc_lines)

def main():
    # Get current directory
    current_dir = os.getcwd()
    print(f"Current directory: {current_dir}")
    print(f"Looking for .c files...\n")
    
    # Find all .c files in current directory and one level deep (no .h files)
    c_files = []
    
    # Current directory
    current_level = list(Path('.').glob('*.c'))
    print(f"Found {len(current_level)} .c files in current directory")
    c_files.extend(current_level)
    
    # One level deep
    subdirs = [d for d in Path('.').iterdir() if d.is_dir() and not d.name.startswith('.')]
    print(f"Found {len(subdirs)} subdirectories")
    
    for subdir in subdirs:
        subdir_files = list(subdir.glob('*.c'))
        if subdir_files:
            print(f"  {subdir.name}/: {len(subdir_files)} .c files")
            c_files.extend(subdir_files)
    
    print()
    
    if not c_files:
        print("ERROR: No .c files found!")
        print("Make sure you're running this script from the directory containing your source files.")
        return
    
    print(f"Total: {len(c_files)} .c files to process")
    print("(Header files will NOT be modified)\n")
    
    # Show files to be processed
    print("Files to process:")
    for f in sorted(c_files):
        print(f"  - {f}")
    print()
    
    response = input("Proceed with replacements? (yes/no): ")
    if response.lower() not in ['yes', 'y']:
        print("Aborted.")
        return
    
    print()
    
    total_malloc_warnings = 0
    
    for filepath in sorted(c_files):
        malloc_count = process_file(str(filepath))
        total_malloc_warnings += malloc_count
        print()
    
    print("=" * 60)
    print("Summary:")
    print(f"  Processed {len(c_files)} .c files")
    print(f"  Backup files created with .bak extension")
    print(f"  Header files (.h) were NOT modified")
    
    if total_malloc_warnings > 0:
        print(f"\n  ⚠ WARNING: {total_malloc_warnings} malloc() call(s) need manual review!")
        print("  These need to be converted to SAFE_MALLOC() with proper type info")
        print("\n  Example conversions:")
        print("    ptr = malloc(size)  -->  ptr = safe_malloc(size, __FILE__, __LINE__)")
        print("    OR")
        print("    ptr = malloc(n * sizeof(type))  -->  SAFE_MALLOC(ptr, type, n)")

if __name__ == '__main__':
    main()
