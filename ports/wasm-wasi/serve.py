#!/usr/bin/env python3
"""
Minimal HTTP server with cross-origin isolation headers for OPFS.
Usage: python3 serve.py [port]
"""
import http.server
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

class CORSHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.wasm': 'application/wasm',
        '.mjs': 'text/javascript',
    }

    def end_headers(self):
        # Cross-origin isolation for OPFS sync access in workers.
        # 'credentialless' is less strict than 'require-corp' and
        # allows loading CDN resources (xterm.js) without CORP headers.
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'credentialless')
        super().end_headers()

print(f'Serving at http://localhost:{PORT}')
print(f'Open in browser to test CircuitPython WASI')
http.server.HTTPServer(('', PORT), CORSHandler).serve_forever()
