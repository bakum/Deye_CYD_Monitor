# Захват serial-лога CYD в файл без интерактивного терминала и без сброса платы.
# `pio device monitor` в фоне не работает: требует интерактивный stdin.
# Запуск (pyserial есть в окружении PlatformIO):
#   ~/.platformio/penv/Scripts/python.exe -I tools/serial_capture.py COM3 60 log.txt
import sys
import time

import serial


def main():
    if len(sys.argv) != 4:
        print("usage: serial_capture.py PORT SECONDS OUTFILE")
        sys.exit(2)
    port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    # DTR/RTS до открытия порта: иначе автосброс ESP32 перезагрузит плату.
    s.dtr = False
    s.rts = False
    s.open()
    end = time.time() + secs
    with open(out, "wb") as f:
        while time.time() < end:
            data = s.read(4096)
            if data:
                f.write(data)
                f.flush()
    s.close()


if __name__ == "__main__":
    main()
