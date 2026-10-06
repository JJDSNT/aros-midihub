# MIDIHub image assets

`midihub-icon-states.png` is the source for the AROS Preferences icon. Its
left half is the normal state and its right half is the selected state.

AROS PNG icons store the selected state as a second complete PNG stream
concatenated after the first PNG's `IEND` chunk. The first stream also carries
an `icOn` chunk identifying the file as a Workbench tool and requesting a
16 KiB stack. This is the same format used for the dedicated Bluetooth
Preferences icon in Bellatrix.

Regenerate `ports/aros/MIDIHub.info` from the repository root with:

```sh
convert images/midihub-icon-states.png \
  -crop 930x793+0+0 -trim +repage -resize 64x64 \
  -background none -gravity center -extent 64x64 -strip \
  -define png:compression-level=9 /tmp/MIDIHubPrefs.png

convert images/midihub-icon-states.png \
  -crop 1053x793+930+0 -trim +repage -resize 64x64 \
  -background none -gravity center -extent 64x64 -strip \
  -define png:compression-level=9 /tmp/MIDIHubPrefs-selected.png

python3 tools/mkicon.py /tmp/MIDIHubPrefs.png \
  ports/aros/MIDIHub.info /tmp/MIDIHubPrefs-selected.png
```

ImageMagick and Python are build-host tools only. The AROS build copies the
finished `.info` and does not need either dependency.
