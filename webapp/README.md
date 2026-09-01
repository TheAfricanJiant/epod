# ePod Digital Signage

The restaurant menu page, browsed by voice. Copy this folder anywhere and run it.

## Run it

```
python serve.py
```

Then open **http://localhost:8000**. On Windows you can double-click `run.bat`;
on macOS and Linux, `./run.sh`. It needs Python 3 and nothing else: no
`pip install`, no build step.

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
   the device, and **demo mode** when it cannot.

If the device is on another address or port:

```
python serve.py --device 192.168.4.1 --port 8080
```

## Why serve.py instead of opening index.html

The page comes from your laptop and the ePod is a different origin, so the
browser blocks a `fetch()` made from a `file://` page. `serve.py` serves the
folder *and* forwards `/api/*` to the device, so both look like one origin and
you do not have to configure anything in the browser.

If you have no Python, the ePod sends CORS headers too, so you can point a
browser straight at `http://192.168.4.1`. You only lose the local page.

## The flow

| Stage | What happens |
|---|---|
| **Sleeping** | Closed eyes, drifting `z z z`. Vision model watching. |
| **Guest detected** | Eyes open, the ePod starts quiet music, **vision switches off** |
| **Welcome** | "Put your earphones on and enjoy the music" (~5 s) |
| **Menu** | Full-screen plates. The voice model is now running. |
| **"next" / "back"** | Moves through the menu |
| **"hello world"** | Green tick, *Order placed* |

Vision and voice never run at the same time. The S3 hands over from one to the
other. Either can be turned off by hand from **Settings** if a model misbehaves.

## Editing the menu

Everything is in **`menu.json`**, so there is no HTML to touch:

```json
{ "image": "images/salad.jpg", "name": "Garden Salad",
  "price": "1200", "note": "Crisp greens, tomato, avocado" }
```

Put new photos in `images/` and add a row. The order in the file is the order on
screen. Roughly square images look best, since they are cropped to a circular
plate.

## Demo keys

The whole flow runs with no hardware, which is useful for rehearsing and as a
fallback if the device misbehaves in front of judges.

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

`seq` increases on each new event. The page acts whenever it changes, so an
event is never missed between polls and never repeated on reload. `POST wake`
runs the whole guest-arrival sequence by hand, which is handy for testing
without a camera.

## Files

```
index.html    the whole app (no dependencies, no build)
menu.json     the menu, edit this
images/       plate photos
serve.py      local server and proxy to the ePod
run.bat       Windows launcher
run.sh        macOS/Linux launcher
```
