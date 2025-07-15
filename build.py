#!/usr/bin/env python3
"""
Simple wrapper for the Selaco Archipelago build script
"""

import sys
import subprocess
from pathlib import Path

def main():
    # Path to the main build script
    build_script = Path(__file__).parent / "build_selaco_archipelago.py"
    
    if not build_script.exists():
        print("ERROR: build_selaco_archipelago.py not found!")
        return 1
    
    # Run the main build script with all arguments passed through
    try:
        result = subprocess.run([sys.executable, str(build_script)] + sys.argv[1:])
        return result.returncode
    except KeyboardInterrupt:
        print("\nBuild interrupted by user")
        return 1
    except Exception as e:
        print(f"Error running build script: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())