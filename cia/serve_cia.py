#!/usr/bin/env python3
"""
serve_cia.py - lets FBI on your 3DS install fear_map.cia straight from your PC.

1. Builds a QR code for http://<your PC's local IP>:8080/fear_map.cia
2. Saves it as fbi_qr.png and opens it
3. Serves the folder until you press Ctrl+C

On the 3DS: FBI -> Remote Install -> Scan QR Code -> point the camera at
the QR code on your screen. The 3DS and the PC must be on the same Wi-Fi.

Needs:  py -m pip install qrcode[pil]
Usage:  py serve_cia.py [path\\to\\fear_map.cia]
"""
import http.server, os, socket, socketserver, sys, threading, webbrowser

PORT = 8080

def local_ip():
    # The address other devices on your network use to reach this PC.
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))   # no packet is sent
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()

def main():
    cia = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "fear_map.cia")
    if not os.path.isfile(cia):
        print(f"Can't find {cia}. Build it first (sh cia/build_cia.sh).")
        sys.exit(1)
    try:
        import qrcode
    except ImportError:
        print("Missing module: run  py -m pip install qrcode[pil]")
        sys.exit(1)

    folder, name = os.path.split(cia)
    ip = local_ip()
    if ip.startswith("127."):
        print("WARNING: couldn't find this PC's network address - is it on Wi-Fi/LAN?")
    url = f"http://{ip}:{PORT}/{name}"

    png = os.path.join(folder, "fbi_qr.png")
    qrcode.make(url).save(png)
    print(f"QR code for {url}\nsaved to {png}")
    print("On the 3DS: FBI -> Remote Install -> Scan QR Code.")
    print("If it can't connect, allow Python through Windows Firewall (private networks).")
    print("Press Ctrl+C here when the install has finished.\n")
    threading.Timer(1.0, lambda: webbrowser.open(png)).start()

    os.chdir(folder)
    handler = http.server.SimpleHTTPRequestHandler
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")

if __name__ == "__main__":
    main()
