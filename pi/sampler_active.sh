#!/bin/bash
# Called when Amiga starts sampling - mute Renoise return

RENOISE_HOST="192.168.86.2"
RENOISE_PORT=8000
TRACK=1

# Send OSC: /renoise/song/track/N/mute
oscsend "$RENOISE_HOST" "$RENOISE_PORT" /renoise/song/track/$TRACK/mute
