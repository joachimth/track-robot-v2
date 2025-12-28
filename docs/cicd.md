# CI/CD and Release Workflow

Automated build, test, and deployment pipelines.

## Overview

Three GitHub Actions workflows provide full automation:
1. **ci.yml** - Build firmware on push/PR
2. **release.yml** - Create releases with binaries
3. **pages.yml** - Deploy web flasher to GitHub Pages

## Workflow 1: Continuous Integration (ci.yml)

**Triggers**: Push to any branch, Pull requests

**Steps**:
1. Checkout code with submodules
2. Install ESP-IDF v5.1.2
3. Run `idf.py build`
4. Upload artifacts (bootloader, partition table, app binary)

**Purpose**: Verify all commits compile successfully

**Artifacts** (available for 7 days):
- `bootloader.bin`
- `partition-table.bin`
- `track-robot.bin`

**Example log**:
```
✓ Checkout code
✓ Setup ESP-IDF v5.1.2
✓ Build firmware
✓ Upload artifacts
```

## Workflow 2: Release Build (release.yml)

**Triggers**: Tag push matching `v*.*.*` (e.g., `v1.0.0`)

**Steps**:
1. Checkout code with submodules
2. Install ESP-IDF v5.1.2
3. Build firmware (release mode)
4. Extract version from tag
5. Create GitHub Release
6. Attach binaries to release
7. Generate `manifest.json` for web flasher
8. Update `latest.json` pointer

**Artifacts attached to release**:
- `bootloader.bin`
- `partition-table.bin`
- `track-robot.bin`
- `manifest.json`

**Manifest format**:
```json
{
  "name": "Tracked Robot Firmware",
  "version": "1.0.0",
  "builds": [{
    "chipFamily": "ESP32",
    "parts": [
      {"path": "bootloader.bin", "offset": 4096},
      {"path": "partition-table.bin", "offset": 32768},
      {"path": "track-robot.bin", "offset": 65536}
    ]
  }]
}
```

**How to create a release**:
```bash
# Commit all changes
git add .
git commit -m "Release v1.0.0"

# Create annotated tag
git tag -a v1.0.0 -m "Release v1.0.0: Initial production release"

# Push commits and tag
git push origin main
git push origin v1.0.0

# CI automatically:
# - Builds firmware
# - Creates release
# - Uploads binaries
# - Generates manifest
# - Deploys web flasher
```

## Workflow 3: GitHub Pages Deploy (pages.yml)

**Triggers**: Push to main branch, Release creation

**Steps**:
1. Checkout web-flasher directory
2. Copy latest manifest.json
3. Build static site (if applicable)
4. Configure GitHub Pages
5. Upload pages artifact
6. Deploy to GitHub Pages

**Result**: Web flasher available at `https://joachimth.github.io/track-robot-v2/`

**Deployment**:
- Automatic on every main branch push
- Also triggered on release creation
- Usually deploys in < 2 minutes

## Release Versioning

**Format**: `vMAJOR.MINOR.PATCH`

**Examples**:
- `v1.0.0` - Initial release
- `v1.0.1` - Bug fix
- `v1.1.0` - New feature
- `v2.0.0` - Breaking change

**Semantic Versioning**:
- MAJOR: Incompatible API changes
- MINOR: Add functionality (backward compatible)
- PATCH: Bug fixes (backward compatible)

## Pinned Dependencies

**ESP-IDF Version**: v5.1.2

**Why pinned?**
- Reproducible builds
- No surprise breaking changes
- Explicit upgrade path

**Upgrading ESP-IDF**:
1. Update version in all workflows
2. Test locally
3. Update sdkconfig.defaults if needed
4. Document changes in CHANGELOG.md

## Build Configuration

**Debug builds** (ci.yml):
- Optimization: Performance
- Assertions: Enabled
- Logging: Debug level

**Release builds** (release.yml):
- Optimization: Size/Performance
- Assertions: Disabled
- Logging: Info level

## Troubleshooting CI

### Build Fails: Submodule Not Found

**Symptom**: `ps3` component not found

**Fix**: Ensure workflow uses `actions/checkout` with `submodules: recursive`

```yaml
- uses: actions/checkout@v3
  with:
    submodules: recursive
```

### Build Fails: ESP-IDF Version Mismatch

**Symptom**: `idf.py not found` or version errors

**Fix**: Verify ESP-IDF installation step uses correct version

```yaml
- name: Setup ESP-IDF
  uses: espressif/esp-idf-ci-action@v1
  with:
    esp_idf_version: v5.1.2
```

### Release Not Created

**Symptom**: Tag pushed but no release appears

**Check**:
- Tag format matches `v*.*.*`
- Workflow has `contents: write` permission
- Build succeeded (check logs)

### Web Flasher Not Updating

**Symptom**: Old firmware version on GitHub Pages

**Fix**:
1. Check pages.yml workflow succeeded
2. Verify manifest.json updated in release
3. Clear browser cache
4. Wait 5 minutes for GitHub CDN update

## Manual Build (Local)

If CI unavailable, build locally:

```bash
cd firmware

# Install dependencies
idf.py install

# Build
idf.py build

# Binaries in: build/
# - build/bootloader/bootloader.bin
# - build/partition_table/partition-table.bin
# - build/track-robot.bin
```

## Future Enhancements

- Unit tests in CI
- Code coverage reporting
- Static analysis (cppcheck, clang-tidy)
- Hardware-in-the-loop testing
- Automated firmware signing

*Last updated: 2025-12-28*
