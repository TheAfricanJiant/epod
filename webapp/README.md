# ePod Digital Signage

Voice-and-vision restaurant menu. Copy this folder anywhere and run it.

## Run it

```
python serve.py
```

Then open **http://localhost:8000**. Windows users can double-click `run.bat`;
macOS/Linux `./run.sh`. Python 3 only — no `pip install`, no build step.

Press **F11** for fullscreen on the day.

## Connect to the ePod

1. On the ePod: **Settings → Wi-Fi AP → ON**
2. Join the network from your laptop:

   | | |
   |---|---|
   | Network | `ePod-Music` |
   | Password | `epodmusicpass` |
   | Device address | `192.168.4.1` |

3. Start the page. The pill in the top-right reads **connected** once it can see
   the device, and **demo mode** when it can't.

Different device IP or port:

```
python serve.py --device 192.168.4.1 --port 8080
```

## Why serve.py rather than opening index.html

The page is served from your laptop but the ePod is a different origin, so a
direct `fetch()` from a `file://` page is blocked by the browser. `serve.py`
serves the folder *and* forwards `/api/*` to the device, so both look like one
origin and nothing has to be configured in the browser.

If Python is unavailable, the ePod also sends CORS headers, so you can point a
browser straight at `http://192.168.4.1` — you lose only the local page.

## The flow

| Stage | What happens |
|---|---|
| **Sleeping** | Soft closed eyes, drifting `z z z`. Vision model watching. |
| **Guest detected** | Eyes open, ePod starts soft music, **vision switches off** |
| **Welcome** | "Put your earphones on and enjoy the music" (~5 s) |
| **Menu** | Full-screen plates. Voice model is now live. |
| **"next" / "back"** | Swipes the menu |
| **"hello world"** | Green tick — *Order placed* |

Vision and voice never run at the same time — the S3 hands over from one to the
other. Either can be killed by hand from **Settings** if a model misbehaves.

## Editing the menu

Everything is in **`menu.json`** — no HTML to touch:

```json
{ "image": "images/salad.jpg", "name": "Garden Salad",
  "price": "1200", "note": "Crisp greens, tomato, avocado" }
```

Drop new photos in `images/` and add a row. Order in the file is the order on
screen. Square-ish images look best; they are cropped to a circular plate.

## Demo keys

The whole flow runs with no hardware — useful for rehearsing, and a safety net
if the device misbehaves in front of judges.

| Key | Action |
|---|---|
| `Space` | wake / advance to menu |
| `←` `→` | previous / next plate |
| `O` | place order |
| `R` | reset to sleeping |

Touch swipe works too.

## Device API

| | |
|---|---|
| `GET /api/state` | `{seq, event, person, idle, vision, voice, playing, tracks}` |
| `POST /api/command` | `vision_on` `vision_off` `voice_on` `voice_off` `wake` `sleep` |

`seq` increments on each new event; the page acts whenever it changes, so an
event is never missed between polls nor replayed on reload. `POST wake` fires
the whole guest-arrival sequence by hand — handy for testing without a camera.

## Files

```
index.html    the entire app (no dependencies, no build)
menu.json     the menu — edit this
images/       plate photos
serve.py      local server + proxy to the ePod
run.bat       Windows launcher
run.sh        macOS/Linux launcher
```
