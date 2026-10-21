"""Loopback-only HTTP proxy integration tests. Pass the proxy_client executable."""

import concurrent.futures
import contextlib
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import time


def request(connection):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = connection.recv(4096)
        assert chunk, "Connection closed before headers"
        data += chunk
    head, body = data.split(b"\r\n\r\n", 1)
    lines = head.decode().split("\r\n")
    headers = dict(line.split(": ", 1) for line in lines[1:])
    headers = {name.lower(): value for name, value in headers.items()}
    length = int(headers.get("content-length", 0))
    while len(body) < length:
        chunk = connection.recv(4096)
        assert chunk, "Connection closed before body"
        body += chunk
    return lines[0], headers, body


def reply(connection, status="200 OK", body=b"ok", extra=""):
    connection.sendall(
        f"HTTP/1.1 {status}\r\nContent-Type: text/plain\r\nContent-Length: {len(body)}\r\n"
        f"{extra}\r\n".encode() + body
    )


@contextlib.contextmanager
def server(handler, connections=1):
    with socket.socket() as listener, concurrent.futures.ThreadPoolExecutor() as pool:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(8)

        def serve():
            for _ in range(connections):
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(6)
                    handler(connection)

        future = pool.submit(serve)
        yield listener.getsockname()[1]
        future.result(timeout=10)


def run(target, proxy="", exclusion="", ca="", body="", code=200, expected="ok"):
    result = subprocess.run(
        [sys.argv[1], target, proxy, exclusion, ca, body],
        capture_output=True, text=True, timeout=10, check=True,
    )
    assert result.stdout.strip() == f"{code}\n{expected}", (result.stdout, result.stderr)


def origin_request(connection, target, body=b""):
    line, headers, received = request(connection)
    assert line == f"{'POST' if body else 'GET'} {target} HTTP/1.1", line
    assert headers["authorization"] == "Bearer origin-only"
    assert "proxy-authorization" not in headers
    assert received == body, received
    return headers


def main():
    with tempfile.TemporaryDirectory(prefix="restgoose-proxy-") as directory:
        certificate = pathlib.Path(directory) / "cert.pem"
        key = pathlib.Path(directory) / "key.pem"
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
             "-keyout", str(key), "-out", str(certificate)],
            check=True, capture_output=True,
        )
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(certificate, key)

        def direct(connection):
            headers = origin_request(connection, "/fragmented")
            assert headers["host"] in (f"127.0.0.1:{port}", f"localhost:{port}")
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n")
            time.sleep(0.1)
            connection.sendall(b"ok")

        with server(direct) as port:
            run(f"http://127.0.0.1:{port}/fragmented")
        print("PASS: direct fragmented response")

        def secure_direct(connection):
            with tls.wrap_socket(connection, server_side=True) as secure:
                direct(secure)

        with server(secure_direct) as port:
            run(f"https://localhost:{port}/fragmented", ca=str(certificate))
        print("PASS: direct HTTPS fragmented response")

        def forward(connection):
            line, headers, body = request(connection)
            assert line == "POST http://origin.invalid:8080/api HTTP/1.1"
            assert headers["host"] == "origin.invalid:8080"
            assert headers["proxy-authorization"] == "Basic proxy-only"
            assert body == b"payload"
            reply(connection)

        with server(forward) as port:
            run("http://origin.invalid:8080/api", f"http://127.0.0.1:{port}", body="payload")
        print("PASS: HTTP forward proxy, nondefault Host port and POST")

        def secure_forward(connection):
            with tls.wrap_socket(connection, server_side=True) as secure:
                forward(secure)

        with server(secure_forward) as port:
            run("http://origin.invalid:8080/api", f"https://localhost:{port}",
                ca=str(certificate), body="payload")
        print("PASS: HTTPS proxy to HTTP destination")

        def tunnel(connection, *, information=False, length=False, redirect=False):
            line, headers, body = request(connection)
            assert line == "CONNECT localhost:8443 HTTP/1.1", line
            assert headers["host"] == "localhost:8443"
            assert headers["proxy-authorization"] == "Basic proxy-only"
            assert "authorization" not in headers
            assert body == b""
            response = b"HTTP/1.1 100 Continue\r\n\r\n" if information else b""
            response += b"HTTP/1.1 200 Connection Established\r\n"
            response += b"Content-Length: 0\r\n" if length else b""
            connection.sendall(response[:15])
            time.sleep(0.02)
            connection.sendall(response[15:] + b"\r\n")
            with tls.wrap_socket(connection, server_side=True) as secure:
                headers = origin_request(secure, "/api", b"payload")
                assert headers["host"] == "localhost:8443"
                if redirect:
                    reply(secure, "302 Found", b"", "Location: /next\r\nX-Old: stale\r\n")
                else:
                    reply(secure)

        for ca in (str(certificate), "ignore"):
            with server(tunnel) as port:
                run("https://localhost:8443/api", f"http://127.0.0.1:{port}", ca=ca, body="payload")
        print("PASS: header-only CONNECT, destination TLS CA/ignore modes and POST")

        with server(lambda connection: tunnel(connection, information=True, length=True)) as port:
            run("https://localhost:8443/api", f"http://127.0.0.1:{port}",
                ca=str(certificate), body="payload")
        print("PASS: informational response and Content-Length: 0 CONNECT")

        requests = 0

        def redirected(connection):
            nonlocal requests
            requests += 1
            if requests == 1:
                tunnel(connection, redirect=True)
            else:
                line, _, _ = request(connection)
                assert line == "CONNECT localhost:8443 HTTP/1.1"
                connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
                with tls.wrap_socket(connection, server_side=True) as secure:
                    origin_request(secure, "/next", b"payload")
                    reply(secure)

        with server(redirected, connections=2) as port:
            run("https://localhost:8443/api", f"http://127.0.0.1:{port}",
                ca=str(certificate), body="payload")
        print("PASS: redirected HTTPS POST establishes a new tunnel and replays body")

        def rejected(connection):
            request(connection)
            reply(connection, "407 Proxy Authentication Required", b"proxy denied")

        with server(rejected) as port:
            run("https://localhost:8443/api", f"http://127.0.0.1:{port}",
                code=407, expected="proxy denied")
        print("PASS: proxy rejection status and body")

        def interrupted_tls(connection):
            request(connection)
            connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            assert connection.recv(1) == b"\x16", "Expected a TLS ClientHello, not origin data"

        with server(interrupted_tls) as port:
            result = subprocess.run(
                [sys.argv[1], "https://localhost:8443/api", f"http://127.0.0.1:{port}",
                 "", str(certificate), "payload"],
                capture_output=True, text=True, timeout=10, check=True,
            )
            assert result.stdout.startswith("2\n"), result.stdout
        print("PASS: interrupted TLS handshake returns a connection error")

        with server(direct) as port:
            run(f"http://127.0.0.1:{port}/fragmented", "http://127.0.0.1:1", "127.0.0.1")
        print("PASS: hostname exclusion bypasses proxy")

        result = subprocess.run(
            [sys.argv[1], "https://localhost/api", "https://localhost:1", "", "", ""],
            capture_output=True, text=True, timeout=5, check=True,
        )
        assert "HTTPS destinations through HTTPS proxies are not supported" in result.stdout
        print("PASS: nested TLS explicitly rejected before connection")


if __name__ == "__main__":
    main()
