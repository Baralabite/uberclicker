#!/usr/bin/env python3
"""Host assertions plus actual loopback listeners checking firmware codec bytes."""
from pathlib import Path
import os, socket, struct, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
openssl = Path(os.environ.get('OPENSSL_PREFIX', '/opt/homebrew/opt/openssl@3'))
flags = ['-I'+str(openssl/'include'), '-L'+str(openssl/'lib')] if openssl.exists() else []
with tempfile.TemporaryDirectory() as tmp:
    core = str(Path(tmp)/'core')
    common = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'include')]
    subprocess.run(common + ['-DHOST_TEST_OPENSSL', str(root/'tests/core_test.cpp'), str(root/'src/radio_protocol.cpp'), str(root/'src/radio_admin.cpp'), str(root/'src/output_codec.cpp')] + flags + ['-lcrypto', '-o', core], check=True)
    subprocess.run([core], check=True)
    admin = str(Path(tmp)/'admin')
    subprocess.run(common + ['-DHOST_TEST_OPENSSL', str(root/'tests/admin_test.cpp'), str(root/'src/radio_protocol.cpp'), str(root/'src/radio_admin.cpp')] + flags + ['-lcrypto', '-o', admin], check=True)
    subprocess.run([admin], check=True)
    health = str(Path(tmp)/'health')
    subprocess.run(common + ['-DHOST_TEST_OPENSSL', str(root/'tests/health_test.cpp'), str(root/'src/radio_protocol.cpp')] + flags + ['-lcrypto', '-o', health], check=True)
    subprocess.run([health], check=True)
    journal = str(Path(tmp)/'journal')
    subprocess.run(common + ['-I'+str(root/'tests/core_stubs'), str(root/'tests/reset_journal_test.cpp'), str(root/'src/reset_journal.cpp'), '-o', journal], check=True)
    subprocess.run([journal], check=True)
    sender = str(Path(tmp)/'sender')
    subprocess.run(common + [str(root/'tests/listener_sender.cpp'), str(root/'src/output_codec.cpp'), str(root/'src/socket_output.cpp'), '-o', sender], check=True)
    for mode, expected in [('osc', b'/next\0\0\0,i\0\0'+struct.pack('>i',100)), ('udp', b'next'), ('tcp', b'next\n')]:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM if mode == 'tcp' else socket.SOCK_DGRAM) as listener:
            listener.settimeout(2)
            listener.bind(('127.0.0.1', 0))
            if mode == 'tcp': listener.listen(1)
            subprocess.run([sender, mode, str(listener.getsockname()[1])], check=True)
            if mode == 'tcp':
                connection, _ = listener.accept()
                with connection:
                    received = b''
                    while True:
                        part = connection.recv(1)  # TCP framing survives arbitrarily split reads.
                        if not part: break
                        received += part
            else: received = listener.recv(256)
            assert received == expected, (mode, received, expected)
            print(f'{mode}: actual local listener verified {received.hex()}')
    # Cancellation after TCP connect emits no bytes; a refused connection does not retry.
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(2)
        subprocess.run([sender, 'cancel', str(listener.getsockname()[1])], check=True)
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(2)
            assert connection.recv(1) == b''
        port = listener.getsockname()[1]
    subprocess.run([sender, 'refused', str(port)], check=True, timeout=2)
    print('TCP cancellation emits no bytes; connection refusal returns without retry')
