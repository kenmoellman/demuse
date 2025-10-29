#!/bin/bash

# Search for SAFE_FREE in files within first-level subdirectories
# and prompt for replacement with SMART_FREE

# Find all files in subdirectories exactly 1 level deep
find . -mindepth 2 -maxdepth 2 -type f | while read -r file; do
    # Check if the file contains SAFE_FREE
    if grep -q "SAFE_FREE" "$file"; then
        # Count occurrences
        count=$(grep -o "SAFE_FREE" "$file" | wc -l)
        
        # Show context
        echo ""
        echo "File: $file"
        echo "Found $count occurrence(s) of SAFE_FREE"
        echo "Context:"
        grep -n "SAFE_FREE" "$file" | head -5
        
        # Prompt user
        read -p "Replace SAFE_FREE with SMART_FREE in this file? (y/n): " answer < /dev/tty
        
        if [[ "$answer" == "y" || "$answer" == "Y" ]]; then
            # Perform replacement
            sed -i 's/SAFE_FREE/SMART_FREE/g' "$file"
            
            echo "✓ Replaced in $file"
        else
            echo "✗ Skipped $file"
        fi
    fi
done

echo ""
echo "Search and replace complete."
