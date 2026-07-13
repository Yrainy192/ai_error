import pymysql
from datetime import datetime

class MysqlClient:
    def __init__(
        self,
        host,
        user,
        password,
        database,
        port=3306
    ):
        self.conn = pymysql.connect(
            host=host,
            user=user,
            password=password,
            database=database,
            port=port,
            charset="utf8mb4"
        )
    def save_record(self, msg):
        action = msg.get("action")
        code = msg.get("code")
        event_time = datetime.strptime(
            msg["time"],
            "%Y%m%d%H%M%S%f"
        )
        with self.conn.cursor() as cursor:
            if action == "up_car":
                sql = """
                INSERT INTO package(
                    car,
                    pc,
                    code,
                    grid,
                    up_time
                )
                VALUES(
                    %s,%s,%s,%s,%s
                )
                ON DUPLICATE KEY UPDATE
                    car=VALUES(car),
                    pc=VALUES(pc),
                    grid=VALUES(grid),
                    up_time=VALUES(up_time)
                """
                cursor.execute(
                    sql,
                    (
                        msg.get("car"),
                        msg.get("pc"),
                        code,
                        msg.get("grid"),
                        event_time,
                    )
                )
            elif action == "down_car":
                sql = """
                UPDATE package
                SET
                    down_time=%s,
                    grid=%s,
                    car=%s,
                    pc=%s
                WHERE code=%s
                """
                cursor.execute(
                    sql,
                    (
                        event_time,
                        msg.get("grid"),
                        msg.get("car"),
                        msg.get("pc"),
                        code
                    )
                )
        self.conn.commit()