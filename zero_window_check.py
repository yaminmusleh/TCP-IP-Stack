import socket, sys, threading, time

# Sends N bytes while NOT reading for a few seconds, then reads the echo back.
# A tiny receive buffer makes the kernel close its window, so the stack's send queue fills,
# the stack closes ITS window too, and both sides have to cope (probes, window updates).
n = int(sys.argv[1]) if len(sys.argv) > 1 else 300000
pause = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
data = bytes((i * 7) % 251 for i in range(n))

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)  # must be set before connect()
s.settimeout(60)
s.connect(("10.0.0.1", 9000))

sender = threading.Thread(target=lambda: s.sendall(data))
sender.start()

time.sleep(pause)  # not reading: the echo piles up and the windows close

got = bytearray()
while len(got) < n:
    chunk = s.recv(65536)
    if not chunk:
        break
    got += chunk

sender.join()
s.close()
print("sent", n, "received", len(got), "match", bytes(got) == data)
