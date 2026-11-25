#!/usr/bin/env python3
"""Serve the SlicerWeb site of this folder on this computer, and open it.

    python serve.py [port]

A browser does not run the site from the files themselves (file://): it has to
be served. Nothing leaves the computer: the server listens on localhost only.
"""
import functools
import http.server
import os
import sys
import webbrowser


class Handler(http.server.SimpleHTTPRequestHandler):
    # Not the types the computer knows: Windows often has a wrong one for .js,
    # and a browser refuses a script or WebAssembly module of the wrong type.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
        ".json": "application/json",
        ".whl": "application/zip",
    }

    def end_headers(self):
        # A build served later on the same port has files of the same names.
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, *args):
        pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    folder = os.path.dirname(os.path.abspath(__file__))
    handler = functools.partial(Handler, directory=folder)
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        url = f"http://localhost:{port}/"
        print(f"SlicerWeb is served at {url} (Ctrl+C to stop)")
        webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
