# dMenu Hint Media

`hint` is an optional section on any entry. If it is missing, behavior is unchanged (text-only hover notes).

## JSON schema

```json
{
  "type": "slider",
  "text": {
    "name": "Preview Example",
    "desc": "This tooltip can show text and animated media."
  },
  "hint": {
    "showOn": "both",
    "media": {
      "type": "flipbook",
      "path": "Data\\SKSE\\Plugins\\dMenu\\hints\\example\\frames",
      "fps": 24,
      "maxW": 420,
      "maxH": 240,
      "loop": true,
      "preload": false,
      "cacheKey": "example.flipbook"
    }
  }
}
```

## Fields

- `hint.showOn`: `note` | `control` | `both` (default: `note`).
- `hint.media.type`: `flipbook` | `gif` | `webp` | `webm`.
- `hint.media.path`:
  - `flipbook`: folder path containing frame images (sorted lexicographically).
  - `gif` / `webp` / `webm`: file path.
- `hint.media.fps`: flipbook playback rate (clamped internally).
- `hint.media.maxW` / `maxH`: preview size cap in tooltip (aspect ratio preserved).
- `hint.media.loop`: animation loops when true.
- `hint.media.preload`: if true, decode/cache is attempted before first hover.
- `hint.media.cacheKey`: optional cache key override (default uses resolved path).

## Runtime safety and fallback

- Tooltip text uses `TextUnformatted`, so `%` characters in descriptions are safe.
- Missing files, invalid formats, decode failures, and unavailable device/context never crash dMenu.
- Any media failure falls back to text-only tooltip when description text exists.
- Animated playback advances only while the tooltip is shown.
- Decoded media is cached with an LRU cap (see `hint_media.ini`).
- WebM hint playback/decode duration is controlled by `HintMediaWebmMaxClipSeconds`.

## `hint_media.ini` controls

File: `Data\SKSE\Plugins\dmenu\hint_media.ini`  
Section: `[Hints]`

- `EnableHintMedia=true`
- `HintMediaCacheMB=128`
- `HintMediaMaxDecodePixels=4194304`
- `HintMediaLogLevel=1` (`0` silent, `1` info, `2` debug)
- `HintMediaPreviewScale=1.0` (`0.5`..`3.0`, multiplies per-entry `maxW`/`maxH`)
- `HintMediaClickToOpen=true` (when `true`, media hints open/close by clicking `(?)` instead of hover)
- `HintMediaWebmMaxBufferedFrames=120` (`12`..`1440`, limits queued WebM frames in memory)
- `HintMediaWebmMaxClipSeconds=30` (`1`..`600`, WebM decode/playback safety cap per clip)
- `HintMediaWebmDecodeBudgetMs=2` (`1`..`16`, per-tick decode time budget for WebM)
- `HintMediaWebmDecodeMaxFramesPerTick=1` (`1`..`8`, per-tick decoded WebM frame cap)

### Low-end profile (suggested)

```ini
[Hints]
EnableHintMedia=true
HintMediaCacheMB=128
HintMediaMaxDecodePixels=2097152
HintMediaPreviewScale=1.25
HintMediaClickToOpen=true
HintMediaWebmMaxBufferedFrames=72
HintMediaWebmMaxClipSeconds=30
HintMediaWebmDecodeBudgetMs=1
HintMediaWebmDecodeMaxFramesPerTick=1
```

Notes:
- Click mode currently applies to entries that display a `(?)` note icon.
- Group/title hints that do not render `(?)` continue using hover behavior.
