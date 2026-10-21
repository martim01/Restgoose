"""Loopback WebSocket proxy tests. Pass websocket_proxy_client as the argument."""

import base64
import hashlib
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from proxy_regression import request, server


def receive_exact(connection, size):
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        assert chunk, "Connection closed during WebSocket frame"
        data += chunk
    return data


def upgrade(connection, expected_uri, authority, proxy=False):
    line, headers, body = request(connection)
    assert line == f"GET {expected_uri} HTTP/1.1", line
    assert headers["host"] == authority, headers
    assert headers["upgrade"] == "websocket"
    assert not body
    if proxy:
        assert headers["proxy-authorization"] == "Basic proxy-only"
    else:
        assert "proxy-authorization" not in headers
    accept = base64.b64encode(hashlib.sha1(
        (headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
    ).digest())
    # Queued messages must not be sent before the WebSocket upgrade.
    connection.settimeout(0.05)
    try:
        premature = connection.recv(1)
    except (socket.timeout, TimeoutError):
        premature = None
    assert premature is None, f"Traffic before WebSocket upgrade: {premature!r}"
    connection.settimeout(6)
    response = (b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    connection.sendall(response[:20])
    time.sleep(0.02)
    connection.sendall(response[20:])
    flags, length = receive_exact(connection, 2)
    assert flags == 0x81 and length == 0x87, (flags, length)
    mask = receive_exact(connection, 4)
    payload = receive_exact(connection, 7)
    assert bytes(value ^ mask[index % 4] for index, value in enumerate(payload)) == b"payload"
    connection.sendall(b"\x81\x0cecho:payload")
    # Allow the client to consume the echo before this test closes the socket.
    time.sleep(0.1)


def run(target, proxy="", exclusion="", ca="", second=None, error=False):
    args = [sys.argv[1], target, proxy, exclusion, ca]
    if second:
        args.append(second)
    result = subprocess.run(args, capture_output=True, text=True, timeout=12, check=True)
    if error:
        assert "opened=0 messages=0 errors=1" in result.stdout, (result.stdout, result.stderr)
    else:
        count = 2 if second else 1
        assert f"opened={count} messages={count} errors=0" in result.stdout, (result.stdout, result.stderr)


def main():
    with tempfile.TemporaryDirectory(prefix="restgoose-ws-proxy-") as directory:
        certificate = pathlib.Path(directory) / "cert.pem"
        key = pathlib.Path(directory) / "key.pem"
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
             "-keyout", str(key), "-out", str(certificate)],
            capture_output=True, check=True,
        )
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(certificate, key)

        def direct(connection):
            upgrade(connection, "/api", f"127.0.0.1:{port}")

        with server(direct) as port:
            run(f"ws://127.0.0.1:{port}/api")
        print("PASS: direct WS with queued data and nondefault Host port")

        def direct_tls(connection):
            with tls.wrap_socket(connection, server_side=True) as secure:
                upgrade(secure, "/api", f"localhost:{port}")

        with server(direct_tls) as port:
            run(f"wss://localhost:{port}/api", ca=str(certificate))
        print("PASS: direct WSS with CA verification")

        def forward(connection):
            upgrade(connection, "http://origin.invalid:8080/api", "origin.invalid:8080", proxy=True)

        with server(forward) as port:
            run("ws://origin.invalid:8080/api", f"http://127.0.0.1:{port}")
        print("PASS: WS via HTTP forward proxy with authentication")

        def secure_forward(connection):
            with tls.wrap_socket(connection, server_side=True) as secure:
                forward(secure)

        with server(secure_forward) as port:
            run("ws://origin.invalid:8080/api", f"https://localhost:{port}", ca=str(certificate))
        print("PASS: WS via HTTPS forward proxy")

        def tunnel(connection, information=False):
            line, headers, body = request(connection)
            assert line == "CONNECT localhost:8443 HTTP/1.1", line
            assert headers["host"] == "localhost:8443"
            assert headers["proxy-authorization"] == "Basic proxy-only"
            assert not body
            response = b"HTTP/1.1 100 Continue\r\n\r\n" if information else b""
            response += b"HTTP/1.1 200 Connection Established\r\n\r\n"
            connection.sendall(response[:12])
            time.sleep(0.02)
            connection.sendall(response[12:])
            with tls.wrap_socket(connection, server_side=True) as secure:
                upgrade(secure, "/api", "localhost:8443")

        for ca in (str(certificate), "ignore"):
            with server(tunnel) as port:
                run("wss://localhost:8443/api", f"http://127.0.0.1:{port}", ca=ca)
        with server(lambda connection: tunnel(connection, information=True)) as port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{port}", ca=str(certificate))
        print("PASS: WSS CONNECT, fragmented/informational headers, CA/ignore modes")

        with server(direct) as port:
            run(f"ws://127.0.0.1:{port}/api", "http://127.0.0.1:1", "127.0.0.1")
        print("PASS: excluded hostname bypasses proxy without credentials")

        with server(direct) as port, server(tunnel) as proxy_port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{proxy_port}",
                "127.0.0.1", str(certificate), second=f"ws://127.0.0.1:{port}/api")
        print("PASS: simultaneous direct and proxied endpoints use independent state")

        def reject(connection):
            request(connection)
            connection.sendall(b"HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n")

        with server(reject) as port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{port}", error=True)
        print("PASS: proxy refusal reports failure once")

        def malformed(connection):
            request(connection)
            connection.sendall(b"this is not HTTP\r\n\r\n")

        with server(malformed) as port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{port}", error=True)
        print("PASS: malformed CONNECT reply reports failure")

        def broken_tls(connection):
            request(connection)
            connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            assert receive_exact(connection, 1) == b"\x16"
            connection.sendall(b"invalid TLS response")
            time.sleep(0.1)

        with server(broken_tls) as port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{port}",
                ca=str(certificate), error=True)
        print("PASS: tunnel TLS failure reports failure once")

        def stalled(connection):
            request(connection)
            time.sleep(3.2)

        with server(stalled) as port:
            run("wss://localhost:8443/api", f"http://127.0.0.1:{port}", error=True)
        print("PASS: stalled CONNECT times out without sending queued frames")

        run("wss://localhost/api", "https://localhost:1", error=True)
        print("PASS: nested TLS rejected explicitly")


if __name__ == "__main__":
    main()
