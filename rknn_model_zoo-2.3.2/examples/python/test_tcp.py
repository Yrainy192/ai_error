import socket
import threading
import json
import time
from datetime import datetime
from random import randint

#HOST = "192.168.60.126"
HOST = "127.0.0.1"
PORT = 9000


# =========================
# 客户端处理逻辑
# =========================
def handle_client(conn, addr):
    print(f"[CONNECT] {addr}")

    running = True
    lock = threading.Lock()

    buffer = ""

    # =========================
    # 推送线程（服务端主动发）
    # =========================
    def send_loop():
        seq = 1
        nonlocal running

        while running:
            try:
                base_time = time.time()

            # =====================
            # 1. 发送 up_car
            # =====================
             #"grid": str(randint(1, 30)),
                msg_up = {
                "car": str(randint(1, 16)),
                "pc": str(randint(1, 5)),
                "code": f"YT{seq:09d}",
                "grid": 10,
                "time": datetime.now().strftime("%Y%m%d%H%M%S%f")[:-3],
                "pick_type": "in",
                "floor": "1",
                "version": "1.1",
                "action": "up_car"
                }

                conn.sendall((json.dumps(msg_up) + "#").encode())
                print(f"[SEND UP {addr}] {msg_up}")

            # =====================
            # 2. 等 3 秒发送 down_car
            # =====================
                time.sleep(10)

                msg_down = msg_up.copy()
                msg_down["time"] = datetime.now().strftime("%Y%m%d%H%M%S%f")[:-3]
                msg_down["action"] = "down_car"

                conn.sendall((json.dumps(msg_down) + "#").encode())
                print(f"[SEND DOWN {addr}] {msg_down}")

                seq += 1

            # =====================
            # 3. 补足 20 秒周期
            # =====================
                elapsed = time.time() - base_time
                sleep_time = max(0, 20 - elapsed)
                time.sleep(sleep_time)

            except Exception as e:
                print(f"[SEND ERROR] {addr}: {e}")
                running = False
                break

    # =========================
    # 接收线程（客户端发）
    # =========================
    def recv_loop():
        nonlocal running, buffer

        while running:
            try:
                data = conn.recv(4096)

                if not data:
                    print(f"[DISCONNECT] {addr}")
                    running = False
                    break

                buffer += data.decode()

                while "#" in buffer:
                    msg_str, buffer = buffer.split("#", 1)
                    msg_str = msg_str.strip()

                    if not msg_str:
                        continue

                    try:
                        msg = json.loads(msg_str)
                        print(f"[RECV {addr}] {msg}")
                    except Exception as e:
                        print(f"[JSON ERROR] {msg_str} -> {e}")

            except Exception as e:
                print(f"[RECV ERROR] {addr}: {e}")
                running = False
                break

    # =========================
    # 启动线程
    # =========================
    t1 = threading.Thread(target=send_loop, daemon=True)
    t2 = threading.Thread(target=recv_loop, daemon=True)

    t1.start()
    t2.start()

    t1.join()
    t2.join()

    try:
        conn.close()
    except:
        pass

    print(f"[CLOSED] {addr}")


# =========================
# 主服务器
# =========================
def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(10)

    print(f"[SERVER START] {HOST}:{PORT}")

    while True:
        conn, addr = server.accept()

        t = threading.Thread(
            target=handle_client,
            args=(conn, addr),
            daemon=True
        )
        t.start()


if __name__ == "__main__":
    main()