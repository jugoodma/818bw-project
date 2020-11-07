# server code

Start the server:
```
docker build --tag 818b:latest .
docker run -it --rm -p 42:42 818b
```

Start a mobile hotspot:
- SSID: `bot`
- PASS: `dankmemes`

Connect to root, typically <http://192.168.1.186>.
If not, run `ipconfig` (windows) or `ifconfig` (linux) and look for `IPv4 Address` (windows) or `inet` (linux).

## API


