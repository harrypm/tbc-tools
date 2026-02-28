# Metadata Improvements Implementation Log

## Overview
This document tracks the implementation of comprehensive metadata improvements as requested:

1. **Bidirectional JSON support** - All tools can now input/output JSON
2. **Y/C support** - VHS-decode style "chroma_.tbc" file support  
3. **Extended metadata format** - ChromaDecoder, ChromaGain, ChromaPhase, LumaNR fields
4. **JSON/SQLite-only reading mode** - ld-analyse can view metadata without TBC files

## Implementation Progress

### Phase 1: Schema Extension ✅ (In Progress)
- [ ] Update SQLite schema in `sqliteio.cpp` to add new ChromaDecoder fields
- [ ] Extend `LdDecodeMetaData::VideoParameters` structure
- [ ] Update read/write methods in SqliteReader/SqliteWriter
- [ ] Increment schema version and add migration logic

### Phase 2: JSON Converter Enhancement
- [ ] Add bidirectional conversion support to `ld-json-converter`
- [ ] Implement SQLite → JSON export functionality
- [ ] Update JSON schema documentation
- [ ] Add command-line options for conversion direction

### Phase 3: Y/C File Support
- [ ] Update `SourceVideo` class to detect and load chroma_.tbc files
- [ ] Modify file detection logic throughout codebase
- [ ] Update command-line tools to handle Y/C file pairs
- [ ] Add Y/C mode indicators to metadata

### Phase 4: ld-analyse Integration
- [ ] Add metadata-only mode to ld-analyse
- [ ] Connect ChromaDecoder dialog to metadata persistence
- [ ] Implement settings save/load functionality
- [ ] Update UI to show metadata-sourced defaults

### Phase 5: ld-chroma-decoder Integration
- [ ] Update ld-chroma-decoder to read ChromaDecoder settings from metadata
- [ ] Use metadata values as defaults when no CLI options provided
- [ ] Maintain CLI option precedence over metadata defaults

## Schema Changes

### New Fields Added to `capture` table:
- `chroma_decoder` TEXT - decoder type ("pal2d", "transform2d", "transform3d", "ntsc1d", "ntsc2d", "ntsc3d", "mono")
- `chroma_gain` REAL - chroma gain multiplier (default 1.0)
- `chroma_phase` REAL - chroma phase adjustment in degrees (default 0.0)
- `luma_nr` REAL - luma noise reduction level in dB (default 0.0)

### Schema Version:
- Updated from version 1 to version 2
- Migration logic added for existing databases

## Files Modified

### Library Changes:
- `src/library/tbc/lddecodemetadata.h` - Added ChromaDecoder fields to VideoParameters
- `src/library/tbc/sqliteio.h` - Added new field handling to reader/writer interfaces  
- `src/library/tbc/sqliteio.cpp` - Updated schema and I/O methods with migration logic

### Tool Changes:
- `src/ld-json-converter/` - Enhanced for bidirectional conversion
- `src/ld-analyse/` - Added metadata-only mode and settings persistence
- `src/ld-chroma-decoder/main.cpp` - Reads metadata defaults

## Testing Strategy
- Create test files with both JSON and SQLite metadata
- Test bidirectional JSON ↔ SQLite conversion  
- Verify Y/C file detection and loading
- Test metadata-only mode in ld-analyse
- Confirm ChromaDecoder settings persistence and application
- Regression testing with existing TBC files

## Notes
- Maintaining backward compatibility with existing metadata files
- CLI options take precedence over metadata defaults
- Y/C support follows vhs-decode conventions for consistency