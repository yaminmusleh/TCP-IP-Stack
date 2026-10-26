import socket, sys

# Sends N bytes to the stack's TCP echo port and checks that the same bytes come back.
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
data = bytes((i * 7) % 251 for i in range(n))

s = socket.create_connection(("10.0.0.1", 9000), timeout=30)
s.sendall(data)

got = bytearray()
while len(got) < n:
    chunk = s.recv(4096)
    if not chunk:
        break
    got += chunk

s.close()
print("sent", n, "received", len(got), "match", bytes(got) == data)
