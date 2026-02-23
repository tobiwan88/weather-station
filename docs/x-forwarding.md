# X Forwarding Setup for SDL Display

The LVGL display library (`lib/lvgl_display`) opens a 320×240 SDL2 window
when the gateway app runs. Inside a Docker devcontainer, the window is sent
to the host display via X11 forwarding.

## Prerequisites (host)

- An X server running on the host (e.g. XQuartz on macOS, native X11 on Linux,
  VcXsrv/Xming on Windows WSL).

## Linux host

```bash
# Allow the local Docker container to connect to your X server:
xhost +local:docker

# Then rebuild/reopen the devcontainer (DISPLAY is forwarded automatically
# via devcontainer.json remoteEnv).
```

## macOS host (XQuartz)

1. Install [XQuartz](https://www.xquartz.org/).
2. In XQuartz → Preferences → Security, enable **Allow connections from network
   clients**.
3. Restart XQuartz.
4. In a terminal:
   ```bash
   xhost +localhost
   ```
5. Ensure `DISPLAY` is set (XQuartz sets it to `:0` or `localhost:0`).
   devcontainer.json forwards `${localEnv:DISPLAY}` into the container.

## Running the gateway with a window

```bash
# Inside the devcontainer:
west build -p always -b native_sim/native/64 apps/gateway \
    --build-dir /home/zephyr/workspace/build/native_sim_native_64/gateway
west build -t run
```

A 320×240 window labelled "WEATHER STATION" should appear on the host display
within a few seconds of startup.

## Headless smoke test (no display server)

Use `SDL_VIDEODRIVER=offscreen` to run without a display:

```bash
SDL_VIDEODRIVER=offscreen \
  printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 15 \
  /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Expected output includes `lvgl_display: init done` and `fake_sensors list`
results with no crashes.
