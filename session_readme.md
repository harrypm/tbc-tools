# TBC Tools - Local Build Test and DMG Packaging Session

## Session Overview
Date: 2026-02-26
Task: Run local build test and improve DMG packaging for GitHub Actions

## Commands Executed

### 1. Initial Setup
```bash
# Check project structure
ls -la

# Install Nix package manager
curl -L https://nixos.org/nix/install | sh

# Source Nix environment
source /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh

# Verify Nix installation
nix --version
# Output: nix (Nix) 2.33.3

# Create Nix config directory
mkdir -p ~/.config/nix
```

### 2. Enable Nix Flakes
Created `/Users/harry/.config/nix/nix.conf`:
```
experimental-features = nix-command flakes
```

### 3. Local Build Test
```bash
# Configure build with CMake
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build with Ninja
nix develop -c ninja -C build

# Run tests
nix develop -c ctest --test-dir build --output-on-failure
```

## Build Results

### Build Status: ✅ SUCCESS
- CMake configuration: Successful
- Ninja build: 229 targets built successfully
- Minor linker warnings about missing EFM lib directories (expected)

### Test Results: ✅ ALL PASSED
```
100% tests passed, 0 tests failed out of 14

Total Test time (real) = 23.78 sec
```

Test breakdown:
- chroma-ntsc-rgb: Passed (4.30 sec)
- chroma-ntsc-ycbcr: Passed (3.96 sec)
- chroma-pal-rgb: Passed (6.41 sec)
- chroma-pal-ycbcr: Passed (6.37 sec)
- decode-pretbc-ntsc-cav: Passed (1.07 sec)
- decode-pretbc-ntsc-clv: Passed (0.30 sec)
- decode-pretbc-pal-cav: Passed (0.64 sec)
- decode-pretbc-pal-clv: Passed (0.27 sec)
- decode-pretbc-pal-ggv: Passed (0.36 sec)
- testfilter: Passed (0.02 sec)
- testlinenumber: Passed (0.01 sec)
- testmetadata: Passed (0.01 sec)
- testvbidecoder: Passed (0.01 sec)
- testvitcdecoder: Passed (0.02 sec)

## Files Modified

### `.github/workflows/build_macos_tools.yml`
**Changes Made:**
1. **Added trigger events**: Push and PR to main branch for continuous testing
2. **Improved version handling**: Dynamic version extraction from git tags or commits
3. **Enhanced dependency bundling**: 
   - Proper dylib dependency discovery with `otool`
   - Automatic dependency copying to app bundle Frameworks
   - Fixed library references using `install_name_tool`
   - Added `@rpath` support for relocated libraries
4. **Better app bundle structure**: Added Frameworks directory
5. **Improved Info.plist**: Added modern macOS app properties
6. **Enhanced DMG creation**: Better layout, compression, and naming
7. **Added verification step**: Check app bundle before DMG creation

### Key Improvements:
- **Dependency Resolution**: Automatically finds and bundles Nix store dependencies
- **Library Path Fixing**: Updates all binary references to use bundled frameworks
- **Better Error Handling**: Graceful failure handling for optional operations
- **Modern macOS Support**: Added dark mode and high-resolution display support

## MacOS Build Fixes Applied
Based on notes from https://github.com/harrypm/ld-decode/tree/refs/heads/MacOS_Build_Fixes:

### Key Changes Made:
1. **Updated runner matrix**: 
   - `macos-latest` for ARM64
   - `macos-15-intel` for x86_64  
   - Added `fail-fast: false` for independent architecture builds

2. **Enhanced script copying**: 
   - Improved shell handling with proper `shopt -s nullglob`
   - Better error handling for missing script directories
   - Handles both `scripts/*` and `scripts/linux/*` patterns

3. **Improved dependency bundling**:
   - Auto-detection of Homebrew prefix with `brew --prefix`
   - Fallback to Qt-specific macdeployqt if available
   - Maintains Nix-based bundling as fallback
   - Proper shell error handling with `set -euxo pipefail`

4. **Architecture-specific optimizations**:
   - Different runners for x86_64 vs ARM64
   - Proper brew path detection for both architectures

### Fixes Not Applied:
- **Qwt framework paths**: Not needed (tbc-tools doesn't use Qwt)
- **LLVM@20 fixes**: Not needed (tbc-tools uses Nix, not Python/llvmlite)

## Current Status
- ✅ Local build test completed successfully
- ✅ All tests passing
- ✅ DMG packaging workflow improved and ready for deployment
- ✅ Workflow supports both x86_64 and arm64 architectures
- ✅ MacOS build fixes applied from ld-decode branch
- ✅ Architecture-specific runner mapping implemented
- ✅ Enhanced dependency detection and bundling
- ✅ Ready for GitHub Actions testing

## Next Steps
1. Test the updated workflow on GitHub Actions
2. Add proper application icon (currently placeholder)
3. Consider code signing for distribution
4. Test DMG installation on clean macOS systems
5. Verify x86_64 and ARM64 builds work independently
