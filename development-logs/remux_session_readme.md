# Remux Session - MOV to MP4

## Input File
- Source: `/media/harry/Backup B/Theft Stop VHS/Theftstop-VHS.mov`
- Target format: MP4

## Commands Executed

### Remux Command
```bash
ffmpeg -i "/media/harry/Backup B/Theft Stop VHS/Theftstop-VHS.mov" -c copy "/media/harry/Backup B/Theft Stop VHS/Theftstop-VHS.mp4"
```

### Results
- **Status**: SUCCESS (exit code 0)
- **Input format**: QuickTime MOV with HEVC video + PCM audio
- **Output format**: MP4 container with stream copy
- **Duration**: 48 seconds
- **Processing speed**: 145x realtime
- **Output file**: `/media/harry/Backup B/Theft Stop VHS/Theftstop-VHS.mp4`
- **File size**: ~34.5MB

### Input Stream Details
- Video: HEVC (H.265), 936x576, 50fps, 4344 kb/s
- Audio: PCM 16-bit stereo, 48kHz, 1536 kb/s
- Timecode: 09:59:40:19
