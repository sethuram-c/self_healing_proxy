#!/usr/bin/env python3

import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


MAX_DELAY_MS = 5000


class MockBackendHandler(BaseHTTPRequestHandler):

    server_version = "MicroProxyMockBackend/1.0"

    def log_message(
        self,
        format_string,
        *args,
    ):
        del format_string
        del args

    def _send_json(
        self,
        status,
        body,
    ):
        encoded = json.dumps(
            body,
            sort_keys=True,
        ).encode("utf-8")

        self.send_response(status)

        self.send_header(
            "Content-Type",
            "application/json",
        )

        self.send_header(
            "Content-Length",
            str(len(encoded)),
        )

        self.send_header(
            "Connection",
            "close",
        )

        self.end_headers()

        self.wfile.write(encoded)

    def do_GET(self):
        parsed = urlparse(self.path)

        query = parse_qs(
            parsed.query
        )

        status = 200
        delay_ms = 0

        if "status" in query:
            try:
                status = int(
                    query["status"][0]
                )
            except ValueError:
                status = 400

        if status < 100 or status > 599:
            status = 400

        if "delay_ms" in query:
            try:
                delay_ms = int(
                    query["delay_ms"][0]
                )
            except ValueError:
                delay_ms = 0

        delay_ms = max(
            0,
            min(
                delay_ms,
                MAX_DELAY_MS,
            ),
        )

        if delay_ms > 0:
            time.sleep(
                delay_ms / 1000.0
            )

        backend_name = (
            self.server.backend_name
        )

        body = {
            "backend": backend_name,
            "method": "GET",
            "path": parsed.path,
            "status": status,
            "delay_ms": delay_ms,
        }

        print(
            f"{backend_name}: "
            f"GET {parsed.path} "
            f"status={status} "
            f"delay_ms={delay_ms}",
            flush=True,
        )

        self._send_json(
            status,
            body,
        )


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Deterministic HTTP backend "
            "for micro-proxy testing"
        )
    )

    parser.add_argument(
        "--name",
        required=True,
        help="backend identity",
    )

    parser.add_argument(
        "--host",
        default="127.0.0.1",
    )

    parser.add_argument(
        "--port",
        type=int,
        required=True,
    )

    return parser.parse_args()


def main():
    args = parse_args()

    server = ThreadingHTTPServer(
        (
            args.host,
            args.port,
        ),
        MockBackendHandler,
    )

    server.backend_name = args.name

    print(
        f"{args.name} listening on "
        f"{args.host}:{args.port}",
        flush=True,
    )

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

    print(
        f"{args.name} shut down cleanly",
        flush=True,
    )


if __name__ == "__main__":
    main()
