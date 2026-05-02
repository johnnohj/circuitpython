# busio.py — WS protocol wrapper for busio.
#
# Shadows the C module (registered as _busio).  Wraps I2C, SPI, UART
# to broadcast bus operations via the WS protocol bus.
#
# Maps to Wippersnapper protobuf:
#   I2C  → i2c.proto (I2CBusInitRequest, I2CDeviceEvent)
#   SPI  → future
#   UART → uart.proto (UARTDeviceAttachRequest, UARTBusResponse)

from _busio import *
from _busio import I2C as _I2C, SPI as _SPI, UART as _UART
import _ws_bus


class I2C(_I2C):
    """I2C with WS protocol broadcasting."""

    def __init__(self, scl, sda, *, frequency=100000, timeout=255):
        super().__init__(scl, sda, frequency=frequency, timeout=timeout)
        self._scl_name = str(scl).split('.')[-1].rstrip(')')
        self._sda_name = str(sda).split('.')[-1].rstrip(')')
        _ws_bus.emit(_ws_bus.I2C_INIT,
            scl_pin=self._scl_name,
            sda_pin=self._sda_name,
            frequency=frequency)

    def writeto(self, address, buffer, *, start=0, end=None, stop=True):
        result = super().writeto(address, buffer, start=start, end=end, stop=stop)
        _ws_bus.emit(_ws_bus.I2C_EVENT,
            address=address,
            direction='write',
            data=bytes(buffer[start:end]))
        return result

    def readfrom_into(self, address, buffer, *, start=0, end=None):
        result = super().readfrom_into(address, buffer, start=start, end=end)
        _ws_bus.emit(_ws_bus.I2C_EVENT,
            address=address,
            direction='read',
            data=bytes(buffer[start:end]))
        return result

    def writeto_then_readfrom(self, address, out_buffer, in_buffer, *,
                               out_start=0, out_end=None, in_start=0, in_end=None):
        result = super().writeto_then_readfrom(
            address, out_buffer, in_buffer,
            out_start=out_start, out_end=out_end,
            in_start=in_start, in_end=in_end)
        _ws_bus.emit(_ws_bus.I2C_EVENT,
            address=address,
            direction='write_read',
            write_data=bytes(out_buffer[out_start:out_end]),
            read_data=bytes(in_buffer[in_start:in_end]))
        return result

    def deinit(self):
        _ws_bus.emit(_ws_bus.I2C_INIT,
            scl_pin=self._scl_name,
            sda_pin=self._sda_name,
            request_type='DELETE')
        super().deinit()


class SPI(_SPI):
    """SPI with WS protocol broadcasting."""

    def __init__(self, clock, MOSI=None, MISO=None, half_duplex=False):
        super().__init__(clock, MOSI=MOSI, MISO=MISO, half_duplex=half_duplex)
        _ws_bus.emit('spi_init',
            clock=str(clock).split('.')[-1].rstrip(')'),
            mosi=str(MOSI).split('.')[-1].rstrip(')') if MOSI else None,
            miso=str(MISO).split('.')[-1].rstrip(')') if MISO else None)

    def deinit(self):
        _ws_bus.emit('spi_init', request_type='DELETE')
        super().deinit()


class UART(_UART):
    """UART with WS protocol broadcasting."""

    def __init__(self, tx=None, rx=None, *, baudrate=9600, **kwargs):
        super().__init__(tx=tx, rx=rx, baudrate=baudrate, **kwargs)
        _ws_bus.emit('uart_init',
            tx=str(tx).split('.')[-1].rstrip(')') if tx else None,
            rx=str(rx).split('.')[-1].rstrip(')') if rx else None,
            baudrate=baudrate)

    def write(self, buf):
        result = super().write(buf)
        _ws_bus.emit('uart_data', direction='write', data=bytes(buf))
        return result

    def deinit(self):
        _ws_bus.emit('uart_init', request_type='DELETE')
        super().deinit()
