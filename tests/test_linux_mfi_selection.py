#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native Demo authentication selection with ephemeral Host-only identities."""

from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


binary, openssl = sys.argv[1:]


def generate(*arguments):
    return subprocess.run([openssl, *map(str, arguments)], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def probe(worker, port, public, certificate):
    deadline = time.monotonic() + 5
    while True:
        try:
            client = socket.create_connection(("127.0.0.1", port), timeout=0.2)
        except OSError:
            if worker.poll() is not None or time.monotonic() >= deadline:
                raise AssertionError("Demo did not start the RTSP listener") from None
            time.sleep(0.05)
        else:
            break
    with client, client.makefile("rb") as stream:
        client.settimeout(3)
        client.sendall(b"POST /auth-setup RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 33\r\n\r\n\x01" + public)
        status = stream.readline().split()
        assert len(status) >= 2 and status[1] == (b"200" if certificate is not None else b"503"), status
        if certificate is None:
            return
        size = 0
        for line in iter(stream.readline, b"\r\n"):
            assert line, "incomplete RTSP response"
            if line.lower().startswith(b"content-length:"):
                size = int(line.split(b":", 1)[1])
        assert 36 < size < 4096, size
        payload = stream.read(size)
        cert_size = int.from_bytes(payload[32:36], "big")
        assert len(payload) == size and payload[36:36 + cert_size] == certificate
        signature_size = int.from_bytes(payload[36 + cert_size:40 + cert_size], "big")
        assert signature_size == 64 and size == 40 + cert_size + signature_size


def run(root, name, public, flags, expected_exit=0, certificate=None):
    state = root / name
    state.mkdir(mode=0o700)
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    command = [binary, "--headless", "--lan", "lo", "--state-dir", str(state),
               "--port", str(port), "--i2c", str(root / "missing-i2c"), *map(str, flags)]
    with (state / "log").open("wb+") as log, subprocess.Popen(command, stdout=log, stderr=log) as worker:
        try:
            if expected_exit == 0:
                probe(worker, port, public, certificate)
                worker.terminate()
            assert worker.wait(timeout=5) == expected_exit
        except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
            log.seek(0)
            raise AssertionError(f"{name}: {error}\n{log.read().decode(errors='replace')}") from error
        finally:
            if worker.poll() is None:
                worker.kill()
    print(f"PASS {name}")


with tempfile.TemporaryDirectory(prefix="carplay-mfi-") as directory:
    root = Path(directory)
    key = root / "identity.pk8"
    key_pem = root / "identity-key.pem"
    cert = root / "identity.der"
    chain = root / "identity.p7b"
    pem = root / "identity.pem"
    wrong_key = root / "wrong.key"
    client_key = root / "client.key"
    generate("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", key_pem)
    generate("pkcs8", "-topk8", "-nocrypt", "-in", key_pem, "-outform", "DER", "-out", key)
    generate("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-outform", "DER", "-out", wrong_key)
    key.chmod(0o600)
    wrong_key.chmod(0o600)
    generate("req", "-new", "-x509", "-key", key, "-keyform", "DER", "-days", "1",
             "-subj", "/CN=CarPlay Demo Test", "-addext", "basicConstraints=critical,CA:FALSE", "-out", pem)
    generate("x509", "-in", pem, "-outform", "DER", "-out", cert)
    generate("crl2pkcs7", "-nocrl", "-certfile", pem, "-outform", "DER", "-out", chain)
    generate("genpkey", "-algorithm", "X25519", "-out", client_key)
    public = generate("pkey", "-in", client_key, "-pubout", "-outform", "DER")[-32:]

    def settings(mode, certificate, private_key):
        return ["--mfi-mode", mode, "--mfi-cert", certificate, "--mfi-key", private_key, "--mfi-protocol", 3]

    run(root, "LAN", public, [])
    run(root, "hardware-missing", public, ["--mfi-mode", "hardware"], expected_exit=1)
    run(root, "auto-without-identity", public, ["--mfi-mode", "auto"], expected_exit=1)
    run(root, "software-incomplete", public, ["--mfi-mode", "software"], expected_exit=2)
    run(root, "software-missing-key", public, settings("software", cert, root / "missing.key"), expected_exit=1)
    run(root, "auto-mismatched-key", public, settings("auto", cert, wrong_key), expected_exit=1)
    key.chmod(0o644)
    run(root, "software-public-key-permissions", public, settings("software", cert, key), expected_exit=1)
    key.chmod(0o600)
    run(root, "software-DER", public, settings("software", cert, key), certificate=cert.read_bytes())
    run(root, "auto-PKCS7", public, settings("auto", chain, key), certificate=chain.read_bytes())
