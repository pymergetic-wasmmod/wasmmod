#!/usr/bin/env python3
# Tiny static HTTP server for wasmmod pack smoke tests.
# Serves a directory tree as-is (VFS mirror). No JSON / resolve API.
#
#   python3 tools/pack_http_server.py --dir packs --port 0 --write-port .http-port
#
# Then: wasm.verify(False); wasm.install_hook("http://127.0.0.1:<port>/")

from __future__ import annotations

import argparse
import http.server
import os
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description="Static pack HTTP server (VFS mirror)")
    ap.add_argument("--dir", required=True, help="Directory to serve (e.g. examples/packs)")
    ap.add_argument("--port", type=int, default=0, help="Listen port (0 = ephemeral)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--write-port", help="Write chosen port to this file")
    args = ap.parse_args()

    root = os.path.abspath(args.dir)
    if not os.path.isdir(root):
        print(f"not a directory: {root}", file=sys.stderr)
        return 1

    os.chdir(root)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt: str, *log_args) -> None:
            pass

    httpd = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    host, port = httpd.server_address[:2]
    if args.write_port:
        with open(args.write_port, "w", encoding="utf-8") as f:
            f.write(str(port))
    print(f"serving {root} on http://{host}:{port}/", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
