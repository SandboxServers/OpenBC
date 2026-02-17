#!/usr/bin/env python3
"""
GameSpy query diagnostic tool for OpenBC server.

Sends GameSpy queries to verify server discovery is working.
Tests direct queries, broadcast queries, and response format.

Usage:
    python3 tools/gs_query.py [host] [port]

    host: Server IP (default: 127.0.0.1)
    port: Server port (default: 22101)
"""

import socket
import sys
import time

def send_query(sock, addr, query, label):
    """Send a GameSpy query and print the response."""
    print(f"\n--- {label} ---")
    print(f"  Sending to {addr[0]}:{addr[1]}: {query}")

    try:
        sock.sendto(query.encode(), addr)
    except Exception as e:
        print(f"  SEND ERROR: {e}")
        return None

    # Wait for response
    sock.settimeout(2.0)
    try:
        data, sender = sock.recvfrom(2048)
        resp = data.decode('ascii', errors='replace')
        print(f"  Response from {sender[0]}:{sender[1]} ({len(data)} bytes):")
        print(f"  {resp}")

        # Parse and validate fields
        fields = {}
        parts = resp.split('\\')
        i = 1  # skip leading empty string
        while i + 1 < len(parts):
            key = parts[i]
            val = parts[i + 1] if i + 1 < len(parts) else ''
            fields[key] = val
            i += 2

        print(f"  Parsed fields:")
        for k, v in fields.items():
            if k and k != 'final':
                print(f"    {k} = {v}")

        # Check required fields
        required = ['gamename', 'hostname', 'numplayers', 'maxplayers',
                     'hostport', 'mapname', 'gametype']
        missing = [f for f in required if f not in fields]
        if missing:
            print(f"  WARNING: Missing fields: {', '.join(missing)}")

        if 'final' not in resp:
            print(f"  WARNING: Response missing \\final\\ terminator")

        if fields.get('gamename') != 'bcommander':
            print(f"  WARNING: gamename is '{fields.get('gamename')}', expected 'bcommander'")

        return resp

    except socket.timeout:
        print(f"  NO RESPONSE (timeout)")
        return None
    except Exception as e:
        print(f"  RECV ERROR: {e}")
        return None

def test_broadcast(port):
    """Send broadcast query to find LAN servers."""
    print(f"\n--- Broadcast LAN query on port {port} ---")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(3.0)

    try:
        sock.sendto(b"\\status\\", ('255.255.255.255', port))
        print(f"  Broadcast sent to 255.255.255.255:{port}")
    except Exception as e:
        print(f"  BROADCAST ERROR: {e}")
        sock.close()
        return

    # Listen for responses
    found = 0
    start = time.time()
    while time.time() - start < 3.0:
        try:
            data, sender = sock.recvfrom(2048)
            found += 1
            resp = data.decode('ascii', errors='replace')
            print(f"  Server found at {sender[0]}:{sender[1]}: {resp[:100]}...")
        except socket.timeout:
            break
        except Exception:
            break

    if found == 0:
        print(f"  No servers found via broadcast on port {port}")
    else:
        print(f"  Found {found} server(s)")

    sock.close()

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 22101

    print(f"GameSpy Query Diagnostic")
    print(f"Target: {host}:{port}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', 0))
    local_port = sock.getsockname()[1]
    print(f"Local port: {local_port}")

    addr = (host, port)

    # Test 1: Basic query
    send_query(sock, addr, "\\basic\\", "Test 1: \\basic\\ query")

    # Test 2: Status query
    send_query(sock, addr, "\\status\\", "Test 2: \\status\\ query")

    # Test 3: Status with queryid (like master server sends)
    send_query(sock, addr, "\\status\\\\queryid\\1.1\\",
               "Test 3: \\status\\ with queryid")

    # Test 4: Info query
    send_query(sock, addr, "\\info\\", "Test 4: \\info\\ query")

    sock.close()

    # Test 5: Broadcast on game port
    test_broadcast(port)

    # Test 6: Broadcast on port 27900 (some GameSpy implementations)
    if port != 27900:
        test_broadcast(27900)

    print("\n--- Done ---")

if __name__ == '__main__':
    main()
