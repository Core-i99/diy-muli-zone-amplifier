from luma.core.interface.serial import i2c, spi
from luma.core.render import canvas
from luma.lcd.device import ili9488

try:
    import RPi.GPIO as GPIO
except Exception:
    print("RPi.GPIO not found. Are you running on a Raspberry Pi?")

serial = spi(port=0, device=0, gpio_DC=23, gpio_RST=24)
device = ili9488(serial, rotate=2)

GPIO.setwarnings(False)
GPIO.setmode(GPIO.BCM)
GPIO.setup(6, GPIO.OUT, initial=GPIO.LOW)


with canvas(device) as draw:
    print(device.bounding_box)
    draw.rectangle(device.bounding_box, outline="white", fill="red", width=5)
    #draw.rectangle([(0,0), (480,320)], fill="black")
    draw.text((30, 40), "Hello World", fill="white", font_size=20)

try:
    while True:
        pass
except KeyboardInterrupt:
    print("Exiting...")
    GPIO.cleanup()
