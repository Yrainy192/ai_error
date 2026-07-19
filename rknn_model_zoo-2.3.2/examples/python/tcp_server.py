import socket
import json


class TCPServer:
    def __init__(
        self,
        dispatcher,
        mysql_client,
        host="127.0.0.1",
        port=9000
    ):
        self.dispatcher = dispatcher
        self.mysql_client = mysql_client
        self.host = host
        self.port = port
        self.processed_down_tasks = set()

    def start(self):
        server = socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM
        )
        server.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_REUSEADDR,
            1
        )
        server.bind(
            (
                self.host,
                self.port
            )
        )
        server.listen(10)
        print(
            f"TCP Server Start: {self.host}:{self.port}"
        )
        while True:
            client, addr = server.accept()
            try:
                data = b""
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                    if b"#" in data:
                        break
                raw_msg = (
                    data.decode("utf-8")
                    .rstrip("#")
                    .strip()
                )
                msg = json.loads(raw_msg)
                print("收到协议:", msg)

                #存库
                self.mysql_client.save_record(msg)
                # 如果后续业务还需要队列

                self.handle_message(
                    msg
                )

                client.send(
                    b'{"result":"success"}#'
                )
            except Exception as e:
                print("处理失败:", e)
                client.send(
                    b'{"result":"fail"}#'
                )
            finally:
                client.close()

    def handle_message(
            self,
            msg
    ):
        # print(
        #     "[TCP] Receive:",
        #     msg
        # )
        #
        # 任务号
        #
        task_code = msg.get(
            "code"
        )
        #
        # ROI
        #
        roi_id = int(
            msg.get(
                "grid",
                0
            )
        )
        #
        # 动作
        #
        action = msg.get(
            "action"
        )
        #
        # ROI检查
        #
        if roi_id <= 0:
            print(
                "[TCP] Invalid ROI"
            )
            return
        # =====================================================
        # up_car 上车不处理
        # =====================================================
        if action == "up_car":
            # print(
            #     "[UP_CAR]",
            #     "ROI={}".format(
            #         roi_id
            #     ),
            #     "TASK={}".format(
            #         task_code
            #     )
            # )
            return

        # =====================================================
        # down_car
        # =====================================================
        elif action == "down_car":
            #
            # 防止重复下车消息
            #
            if task_code in self.processed_down_tasks:
                print(
                    "[TCP] Duplicate Down Task:",
                    task_code
                )
                return
            #
            # 记录已处理任务
            #
            self.processed_down_tasks.add(
                task_code
            )
            # print(
            #     "[TOKEN]",
            #     "ROI={}".format(
            #         roi_id
            #     ),
            #     "TASK={}".format(
            #         task_code
            #     )
            # )
            #
            # ROI Token +1
            #
            self.dispatcher.queue.put({
                "type": "token_add",
                "roi": roi_id,
                "count": 1
            })
            return
        # =====================================================
        # 未知协议
        # =====================================================
        else:
            print(
                "[TCP] Unknown Action:",
                action
            )