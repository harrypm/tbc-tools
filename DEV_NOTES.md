# Development Notes - tbc-tools

## GitHub Actions Workflow Critical Rules

### Workflow Name Format (REQUIRED)
The Linux workflow **MUST** use this exact format:
```yaml
name: "Build Linux tools"
```

**Key points:**
- Must use **double quotes** around the name
- No single quotes
- No unquoted names
- The exact string must appear on line 1

This is the ONLY format that GitHub Actions recognizes for displaying the workflow name. Any deviation breaks the workflow visibility.

**Verified working commit:** `36973ee`

### Why This Matters
GitHub Actions parses the workflow YAML at a very low level. If the format is even slightly different:
- The workflow name field appears as the filename (e.g., `.github/workflows/build_linux_tools.yml`)
- The workflow doesn't appear in the GitHub Actions UI properly
- It may not trigger on release events

### Testing Changes
Before committing any workflow changes:
1. Make the change
2. Commit and push
3. Check API response: `curl -s "https://api.github.com/repos/owner/repo/actions/workflows" | python3 -m json.tool`
4. Verify the `"name"` field shows the display name, NOT the filepath
5. Only if correct, continue; otherwise revert

## AppImage Build Process

The Linux workflow uses:
1. **linuxdeploy** - AppImage creation tool
2. **Nix** - Build environment and dependency management
3. **Oracle Linux 8 container** - Build environment

### Current Build Flow
1. Checkout code
2. Set up Nix development environment
3. Build binaries with CMake via Nix
4. Download linuxdeploy tools
5. Bundle Qt plugins and dependencies
6. Create AppImage with custom AppRun script
7. Upload as artifact

### AppRun Script
The AppRun script enables:
- Default GUI execution: `./appimage.AppImage` → runs ld-analyse
- CLI tool passthrough: `./appimage.AppImage tool-name args` → runs any bundled tool

## Recovery Procedure

If workflow breaks:
```bash
# Get back to verified working state
git checkout 36973ee -- .github/workflows/build_linux_tools.yml
git commit -m "Restore working Linux workflow"
git push origin main

# Verify it works
curl -s "https://api.github.com/repos/harrypm/tbc-tools/actions/workflows" | grep "Build Linux tools"
```

## Workflow Format Lock

**CRITICAL FINDING:** The workflow file structure is completely locked. Any changes to:
- The build steps
- The environment variables
- The container configuration
- The job structure

...cause GitHub Actions to fail parsing the workflow name field.

Attempted: Replacing linuxdeploy with appimagetool
- Changed only env variables and build steps
- Kept name/on/jobs structure identical
- **Result:** GitHub Actions stopped recognizing the workflow (showed filename instead of display name)

### Why This Happens
GitHub's workflow parser has a very strict parser that appears to be tightly coupled to the specific YAML structure. Even minor changes to the implementation (steps, environment) seem to affect name field parsing.

### Current Working Version
Commit 36973ee contains the ONLY known working configuration:
- linuxdeploy + Qt6 plugins
- Oracle Linux 8 container
- DNF-based setup
- This exact structure must be preserved

### For Future Work
If AppImage improvements are needed, they must be made by:
1. Searching for other projects' working GitHub Actions workflows for AppImage
2. Starting from a completely different workflow file structure
3. Testing incrementally at each change
4. **Never** modifying the working 36973ee file
