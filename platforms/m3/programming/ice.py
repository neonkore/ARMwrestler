#!/usr/bin/env python

################################################################################

import sys
import errno
import socket
import struct
import time
from copy import copy
import logging
logging.basicConfig(level=logging.INFO, format="%(message)s")
logger = logging.getLogger('ice')

try:
    import threading
    import Queue
except ImportError:
    logger.warn("Your python installation does not support threads.")
    logger.warn("")
    logger.warn("Please install a version of python that supports threading.")
    raise

try:
    import serial
except ImportError:
    logger.warn("You do not have the pyserial library installed.")
    logger.warn("")
    logger.warn("For debian-based systems (e.g. Ubuntu):")
    logger.warn("\tsudo apt-get install pyserial")
    logger.warn("For rpm-based systems (e.g. Red Hat):")
    logger.warn("\tsudo yum install pyserial")
    logger.warn("For more installation instructions + see:")
    logger.warn("\thttp://pyserial.sourceforge.net/pyserial.html#installation")
    raise

################################################################################

class ICE(object):
    VERSIONS = ((0,1),(0,2))
    VERSION_0_2_METHODS = (
            'query_capabilities',
            'goc_get_onoff',
            'goc_set_onoff',
            'mbus_send',
            'mbus_set_full_prefix',
            'mbus_get_full_prefix',
            'mbus_set_short_prefix',
            'mbus_get_short_prefix',
            'mbus_set_full_snoop_prefix',
            'mbus_get_full_snoop_prefix',
            'mbus_set_short_snoop_prefix',
            'mbus_get_short_snoop_prefix',
            'mbus_set_broadcast_channel_mask',
            'mbus_get_broadcast_channel_mask',
            'mbus_set_broadcast_channel_snoop_mask',
            'mbus_get_broadcast_channel_snoop_mask',
            'mbus_get_master_onoff',
            'mbus_set_master_onoff',
            'mbus_get_mbus_clock',
            'mbus_set_mbus_clock',
            'mbus_get_should_interrupt',
            'mbus_set_should_interrupt',
            'mbus_get_use_priority',
            'mbus_set_use_priority',
            'ein_send',
            'b_defragger',
            'B_defragger',
            )
    ONEYEAR = 365 * 24 * 60 * 60

    class ICE_Error(Exception):
        '''
        A base class for all exceptions raised by this module
        '''
        pass

    class FormatError(ICE_Error):
        '''
        Something in the ICE protocol communicating with the board went wrong.

        This error should never be encountered in normal use.
        '''
        pass

    class ParameterError(ICE_Error):
        '''
        An illegal parameter was passed.

        This may be raised by the ICE library if it can determine in advance
        that the reqeust is illegal (e.g. out of range), or by the ICE board if
        the board rejects the desired setting (e.g. not configurable)
        '''

    class NAK_Error(ICE_Error):
        '''
        Raised when an unexpected NAK is returned
        '''
        pass

    class VersionError(ICE_Error):
        '''
        A method was called that this ICE board does not support.
        '''

    #def __getattr__(self, name):
    #    if (self.minor < 2) and (name in VERSION_0_2_METHODS):
    #        raise VersionError, "ICE >= v0.2 required. Connected to ICE v0." + str(self.minor)
    #    return object.__getattr__(name)

    def __init__(self):
        '''
        An ICE object.

        Most methods are not usuable until connect() has been called.
        '''

        self.event_id = 0
        self.last_event_id = -1
        self.sync_queue = Queue.Queue(1)

        self.msg_handler = {}
        self.d_lock = threading.Lock()
        self.d_frag = ''
        self.msg_handler['d'] = self.d_defragger
        self.b_lock = threading.Lock()
        self.b_frag = ''
        self.msg_handler['b'] = self.b_defragger
        self.B_lock = threading.Lock()
        self.B_frag = ''
        self.msg_handler['B'] = self.B_defragger

    def connect(self, serial_device, baudrate=115200):
        '''
        Opens a connection to the ICE board.

        The ICE object configuration (e.g. message handlers) cannot be safely
        changed after this method is invoked.
        '''
        self.dev = serial.Serial(serial_device, baudrate)
        if self.dev.isOpen():
            logger.info("Connected to serial device at " + self.dev.portstr)
        else:
            raise self.ICE_Error, "Failed to connect to serial device"

        self.comm_thread = threading.Thread(target=self.communicator)
        self.comm_thread.daemon = True
        self.comm_thread.start()

        self.negotiate_version()

    def spawn_handler(self, msg_type, event_id, length, msg):
        try:
            t = threading.Thread(target=self.msg_handler[msg_type],
                    args=(msg_type, event_id, length, msg))
            t.daemon = True
            t.start()
        except KeyError:
            logger.warn("WARNING: No handler registered for message type: " +
                    str(msg_type))
            logger.warn("Known Types:")
            for t,f in self.msg_handler.iteritems():
                logger.warn("%s\t%s" % (t, str(f)))
            logger.warn("         Dropping packet:")
            logger.warn("")
            logger.warn("    Type: %s" % (msg_type))
            logger.warn("Event ID: %d" % (event_id))
            logger.warn("  Length: %d" % (length))
            logger.warn(" Message:" + msg.encode('hex'))

    def communicator(self):
        while True:
            msg_type, event_id, length = self.dev.read(3)
            msg_type = ord(msg_type)
            event_id = ord(event_id)
            length = ord(length)
            msg = self.dev.read(length)

            if event_id == self.last_event_id:
                logger.warn("WARNING: Duplicate event_id! THIS IS A BUG [somewhere]!!")
                logger.warn("         Dropping packet:")
                logger.warn("")
                logger.warn("    Type: %d" % (msg_type))
                logger.warn("Event ID: %d" % (event_id))
                logger.warn("  Length: %d" % (length))
                logger.warn(" Message:" + msg.encode('hex'))
            else:
                self.last_event_id = event_id

            if msg_type in (0,1):
                # Ack / Nack response from a synchronous message
                try:
                    if msg_type == 0:
                        logger.debug("Got an ACK packet. Event: " + str(event_id))
                    else:
                        logger.info("Got a NAK packet. Event:" + str(event_id))
                    self.sync_queue.put((msg_type, msg))
                except Queue.Full:
                    logger.warn("WARNING: Synchronization lost. Unsolicited ACK/NAK.")
                    logger.warn("         Dropping packet:")
                    logger.warn("")
                    logger.warn("    Type: %s" % (["ACK","NAK"][msg_type]))
                    logger.warn("Event ID: %d" % (event_id))
                    logger.warn("  Length: %d" % (length))
                    logger.warn(" Message:" + msg.encode('hex'))
            else:
                msg_type = chr(msg_type)
                logger.debug("Got an async message of type: " + msg_type)
                self.spawn_handler(msg_type, event_id, length, msg)

    def string_to_masks(self, mask_string):
        ones = 0
        zeros = 0
        idx = len(mask_string)
        for c in mask_string:
            idx -= 1
            if c == '1':
                ones |= (1 << idx)
            elif c == '0':
                zeros |= (1 << idx)
            elif c in ('x', 'X', ' '):
                continue
            else:
                raise self.FormatError, "Illegal character: >>>" + c + "<<<"

    def masks_to_strings(self, ones, zeros, length):
        s = ''
        for l in xrange(length):
            o = bool(ones & (1 << l))
            z = bool(zeros & (1 << l))
            if o and z:
                raise self.FormatError, "masks_to_strings has req 1 and req 0?"
            if o:
                s += '1'
            elif z:
                s += '0'
            else:
                s += 'x'
        return s

    def d_defragger(self, msg_type, event_id, length, msg):
        '''
        Helper function to defragment 'd' type I2C messages before forwarding.

        This helper is installed by default for 'd' messages. It will attempt to
        call a helper registered under the name 'd+' when a complete message has
        been received. The message will be assigned the event id of the last
        received fragment.

        It may be safely overridden.
        '''
        with self.d_lock:
            assert msg_type == 'd'
            self.d_frag += msg
            # XXX: Make version dependent
            if length != 255:
                sys.stdout.flush()
                logger.debug("Got a complete I2C transaction of length %d bytes. Forwarding..." % (len(self.d_frag)))
                sys.stdout.flush()
                self.spawn_handler('d+', event_id, len(self.d_frag), copy(self.d_frag))
                self.d_frag = ''
            else:
                logger.debug("Got an I2C fragment... thus far %d bytes received:" % (len(self.d_frag)))

    def b_defragger(self, msg_type, event_id, length, msg):
        '''
        Helper function to defragment 'b' type MBus messages before forwarding.

        This helper is installed by default for 'b' messages. It will attempt to
        call a helper registered under the name 'b+' when a complete message has
        been received. The message will be assigned the event id of the last
        received fragment.

        It may be safely overridden.
        '''
        with self.b_lock:
            assert msg_type == 'b'
            self.b_frag += msg
            # XXX: Make version dependent
            if length != 255:
                sys.stdout.flush()
                logger.debug("Got a complete MBus message of length %d bytes. Forwarding..." % (len(self.b_frag)))
                sys.stdout.flush()
                self.spawn_handler('b+', event_id, len(self.b_frag), copy(self.b_frag))
                self.b_frag = ''
            else:
                logger.debug("Got a MBus fragment... thus far %d bytes received:" % (len(self.b_frag)))

    def B_defragger(self, msg_type, event_id, length, msg):
        '''
        Helper function to defragment 'B' type snooped MBus messages before forwarding.

        This helper is installed by default for 'B' messages. It will attempt to
        call a helper registered under the name 'B+' when a complete message has
        been received. The message will be assigned the event id of the last
        received fragment.

        It may be safely overridden.
        '''
        with self.B_lock:
            assert msg_type == 'B'
            self.B_frag += msg
            # XXX: Make version dependent
            if length != 255:
                sys.stdout.flush()
                logger.debug("Got a complete snooped MBus message. Length %d bytes. Forwarding..." % (len(self.B_frag)))
                sys.stdout.flush()
                self.spawn_handler('B+', event_id, len(self.B_frag), copy(self.B_frag))
                self.B_frag = ''
            else:
                logger.debug("Got a snoop MBus fragment... thus far %d bytes received:" % (len(self.B_frag)))

    def send_message(self, msg_type, msg='', length=None):
        if len(msg_type) != 1:
            raise self.FormatError, "msg_type must be exactly 1 character"

        if len(msg) > 255:
            raise self.FormatError, "msg too long. Maximum msg is 255 bytes"

        if length is None:
            length = len(msg)

        buf = struct.pack("BBB", ord(msg_type), self.event_id, length)
        self.event_id = (self.event_id + 1) % 256
        self.dev.write(buf + msg)

        # Ugly hack so python allows keyboard interrupts
        return self.sync_queue.get(True, self.ONEYEAR)

    def send_message_until_acked(self, msg_type, msg='', length=None, tries=5):
        while tries:
            ack, msg = self.send_message(msg_type, msg, length)
            if ack == 0:
                return msg
            tries -= 1

        raise self.NAK_Error

    def negotiate_version(self):
        '''
        Establish communication with an ICE board.

        This function is called automatically by __init__ and should not be
        called directly.
        '''
        logger.info("This library supports versions...")
        for major, minor in ICE.VERSIONS:
            logger.info("\t%d.%d" % (major, minor))

        logger.debug("Sending version probe")
        resp = self.send_message_until_acked('V')
        if (len(resp) is 0) or (len(resp) % 2):
            raise self.FormatError, "Version response: " + resp

        logger.info("This ICE board supports versions...")
        self.major = None
        self.minor = None
        while len(resp) > 0:
            major, minor = struct.unpack("BB", resp[:2])
            resp = resp[2:]
            if self.major is None and (major, minor) in ICE.VERSIONS:
                self.major = major
                self.minor = minor
                logger.info("\t%d.%d **Chosen version" % (major, minor))
            else:
                logger.info("\t%d.%d" % (major, minor))

        if self.major is None:
            logger.warn("No versions in common. Version negotiation failed.")
            raise self.ICE_Error

        if self.major != 0:
            logger.error("Major version number bump. Need to re-examine python versioning")
            raise self.ICE_Error

        self.send_message_until_acked('v', struct.pack("BB", self.major, self.minor))

    def _fragment_sender(self, msg_type, msg):
        '''
        Internal. (helper for {i2c,goc}_send
        '''
        # XXX: Make version dependent?
        FRAG_SIZE = 255

        sent = 0
        logger.debug("Sending %d byte message (in %d byte fragments)" % (len(msg), FRAG_SIZE))
        while len(msg) >= FRAG_SIZE:
            ack,resp = self.send_message(msg_type, msg[0:FRAG_SIZE])
            if ack == 1: # (NAK)
                return sent + ord(resp)
            msg = msg[FRAG_SIZE:]
            sent += FRAG_SIZE
            logger.debug("\tSent %d byte s, %d remaining" % (sent, len(msg)))
        logger.debug("Sending last messag e, %d bytes long" % (len(msg)))
        ack,resp = self.send_message(msg_type, msg)
        if ack == 1:
            return sent + ord(resp)
        sent += len(msg)
        return sent

    ## QUERY ICE ##
    def query_capabilities(self):
        '''
        Queries ICE board for available hardware frontends.

        This interface is very raw and needs to be wrapped in something more
        user-friendly and library-esque. It currently returns the raw array of
        characters from the ICE board, which requires the caller to know the
        ICE protocol.
        '''
        resp = self.send_message_until_acked('?', struct.pack("B", ord('c')))
        return resp

    ## GOC ##
    GOC_SPEED_DEFAULT_HZ = .625

    def _goc_display_delay(self, msg, event):
        try:
            freq = self.goc_freq
        except AttributeError:
            freq = GOC_SPEED_DEFAULT_HZ

        num_bits = len(msg) * 8
        t = num_bits / freq
        logger.info("Sleeping for %f seconds while it blinks..." % (t))
        while (t > 1):
            sys.stdout.write("\r\t\t\t\t\t\t")
            sys.stdout.write("\r\t%f remaining..." % (t))
            sys.stdout.flush()
            t -= 1
            if event.is_set():
                return
            time.sleep(1)
        time.sleep(t)

    def goc_send(self, msg, show_progress=True):
        '''
        Blinks a message via GOC.

        Takes a raw byte stream (e.g. "0xaa".decode('hex')).
        Returns the number of bytes actually sent.

        Long messages may be fragmented between the ICE library and the ICE
        FPGA. These fragments will be combined on the ICE board, and given the
        significantly lower bandwidth of the GOC interface, there should be no
        interruption in message transmission.
        '''
        if show_progress:
            e = threading.Event()
            t = threading.Thread(target=self._goc_display_delay, args=(msg,e))
            t.daemon = True
            t.start()
            ret = self._fragment_sender('f', msg)
            e.set()
            t.join()
        else:
            ret = self._fragment_sender('f', msg)
        return ret

    def goc_set_frequency(self, freq_in_hz):
        '''
        Sets the GOC frequency.
        '''
        # Send a 3-byte value N, where 2 MHz / N == clock speed
        NOMINAL = int(2e6)
        setting = NOMINAL / freq_in_hz;
        packed = struct.pack("!I", setting)
        if packed[0] != '\x00':
            raise self.ParameterError, "Out of range."
        msg = struct.pack("B", ord('c')) + packed[1:]
        self.send_message_until_acked('o', msg)

        self.goc_freq = freq_in_hz

    def goc_get_onoff(self):
        '''
        Get the current ambient GOC power.
        '''
        resp = self.send_message_until_acked('O', struct.pack("B", ord('o')))
        if len(resp) != 1:
            raise self.FormatError, "Wrong response length from `Oo': " + str(resp)
        onoff = struct.unpack("B", resp)
        return bool(onoff)

    def goc_set_onoff(self, onoff):
        '''
        Turn the GOC light on or off.

        The GOC will blink as normal when goc_send is called, this simply sets
        the state of the GOC light when it's not doing anything else (e.g. so
        you can leave the light on for charging or something similar)
        '''
        msg = struct.pack("BB", ord('o'), onoff)
        self.send_message_until_acked('o', msg)

    ## I2C ##
    def i2c_send(self, addr, data):
        '''
        Sends an I2C message.

        Addr should be a single byte address.
        Data should be packed binary data, as returned by struct.pack

        The return value is the number of bytes actually sent *including the
        address byte*.

        Long messages may be fragmented between the ICE library and the ICE
        FPGA. On the I2C wire, this will appear as windows of time where the I2C
        clock is stretched for a period of time.  A faster baud rate between the
        PC host and the ICE FPGA will help mitigate this.
        '''

        msg = struct.pack("B", addr) + data
        return self._fragment_sender('d', msg)

    def i2c_get_speed(self):
        '''
        Get the clock speed of the ICE I2C driver in kHz.
        '''
        ack,msg = self.send_message('I', struct.pack("B", ord('c')))
        if ack == 0:
            if len(msg) != 1:
                raise self.FormatError
            return struct.unpack("B", msg) * 2

        ret = ord(msg[0])
        msg = msg[1:]
        if ret == errno.ENODEV:
            # XXX Generalize me w.r.t. version?
            return 100
        else:
            raise self.ICE_Error, "Unknown Error"

    def i2c_set_speed(self, speed):
        '''
        Set the clock speed of the ICE I2C driver in kHz.

        The accepted range of speeds is [2,400] kHz with steps of undefined
        increments. The actual set speed is returned.

        Raises an ICE_Error if the speed was not set.

        Note: This does *NOT* affect the clock speed of any M3 I2C drivers.
              That requires sending DMA messages to each of the M3 I2C
              controllers that you would like to change the speed of.
        '''
        if speed < 2:
            speed = 2
        elif speed > 400:
            speed = 400

        speed /= 2
        ack,msg = self.send_message('i', struct.pack("BB", ord('c'), speed))

        if ack == 0:
            return speed

        ret = ord(msg[0])
        msg = msg[1:]
        if ret == errno.EINVAL:
            raise self.ICE_Error, "ICE reports: Invalid argument."
        elif ret == errno.ENODEV:
            raise self.ICE_Error, "Changing I2C speed not supported."

    def i2c_set_address(self, address=None):
        '''
        Set the I2C address(es) of the ICE peripheral.

        The ICE board will ACK messages sent to any address that matches the
        mask set by this function. The special character 'x' is used to signify
        don't-care bits. As example, to pretend to be the DSP layer:

           address = "1001 100x"

        Spaces are permitted and ignored. To disable this feature, set the
        address to None.

        Default Value: DISABLED.
        '''
        if address is None:
            ones, zeros = (0xff, 0xff)
        else:
            if len(address) != 8:
                raise self.FormatError, "Address must be exactly 8 bits"
            ones, zeros = self.string_to_masks(address)
        self.send_message_until_acked('i', struct.pack("BBB", ord('a'), ones, zeros))

    ## MBus ##
    def mbus_send(self, addr, data):
        '''
        Sends an MBus message.

        Addr may be a short address or long address. In either case, it should
        be packed binary data (e.g. struct.pack or 'a5'.decode('hex'))
        Data should be packed binary data, as returned by struct.pack

        The return value is the number of bytes actually sent *including the
        address byte(s)*.

        Long messages may be fragmented between the ICE library and the ICE
        FPGA. On the wire, this should not be noticeable as the PC<-->ICE bridge
        is much faster than the MBus. If this is an issue, you must keep the
        transaction size below the ICE fragmentation limit (less than 255 bytes
        for combined address + data).
        '''
        return self._fragment_sender('b', addr+data)

    def mbus_set_full_prefix(self, prefix=None):
        '''
        Set the full prefix(es) of the ICE peripheral.

        The ICE board will ACK messages sent to any address that matches the
        mask set by this function. The special character 'x' is used to signify
        don't-care bits.

        Spaces are permitted and ignored. To disable this feature, set the
        address to None.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xfffff, 0xfffff)
        else:
            if len(prefix) != 20:
                raise self.FormatError, "Prefix must be exactly 20 bits"
            ones, zeros = self.string_to_masks(prefix)
        ones <<= 4
        zeros <<= 4
        self.send_message_until_acked('m', struct.pack("B"*(1+6),
            ord('l'),
            (ones >> 16) & 0xff,
            (ones >> 8) & 0xff,
            ones & 0xff,
            (zeros >> 16) & 0xff,
            (zeros >> 8) & 0xff,
            zeros & 0xff,
            ))

    def mbus_get_full_prefix(self):
        '''
        Get the full prefix(es) set for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('l')))
        if len(resp) != 6:
            raise self.FormatError, "Full prefix response should be 6 bytes"
        o_hig, o_mid, o_low, z_hig, z_mid, z_low = struct.unpack("BBBBBB", resp)
        ones = o_low | o_mid << 8 | o_hig << 16
        zeros = z_low | z_mid << 8 | z_hig << 16
        ones >>= 4
        zeros >>= 4
        if ones == 0xfffff and zeros == 0xfffff:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 20)

    def mbus_set_short_prefix(self, prefix=None):
        '''
        Set the short prefix(es) of the ICE peripheral.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xf, 0xf)
        else:
            if len(prefix) != 4:
                raise self.FormatError, "Prefix must be exactly 4 bits"
            ones, zeros = self.string_to_masks(prefix)
        ones <<= 4
        zeros <<= 4
        self.send_message_until_acked('m', struct.pack("B"*(1+2),
            ord('s'),
            ones,
            zeros,
            ))

    def mbus_get_short_prefix(self):
        '''
        Get the short prefix(es) set for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('s')))
        if len(resp) != 2:
            raise self.FormatError, "Full prefix response should be 2 bytes"
        ones, zeros = struct.unpack("BB", resp)
        ones >>= 4
        zeros >>= 4
        if ones == 0xf and zeros == 0xf:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 4)

    def mbus_set_full_snoop_prefix(self, prefix=None):
        '''
        Set the full snoop prefix(es) of the ICE peripheral.

        The ICE board will report, but not ACK, any messages sent to any address
        that matches the mask set by this function. The special character 'x' is
        used to signify don't-care bits.

        Spaces are permitted and ignored. To disable this feature, set the
        address to None.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xfffff, 0xfffff)
        else:
            if len(prefix) != 20:
                raise self.FormatError, "Prefix must be exactly 20 bits"
            ones, zeros = self.string_to_masks(prefix)
        ones <<= 4
        zeros <<= 4
        self.send_message_until_acked('m', struct.pack("B"*(1+6),
            ord('L'),
            (ones >> 16) & 0xff,
            (ones >> 8) & 0xff,
            ones & 0xff,
            (zeros >> 16) & 0xff,
            (zeros >> 8) & 0xff,
            zeros & 0xff,
            ))

    def mbus_get_full_snoop_prefix(self):
        '''
        Get the full snoop prefix(es) set for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('L')))
        if len(resp) != 6:
            raise self.FormatError, "Full prefix response should be 6 bytes"
        o_hig, o_mid, o_low, z_hig, z_mid, z_low = struct.unpack("BBBBBB", resp)
        ones = o_low | o_mid << 8 | o_hig << 16
        zeros = z_low | z_mid << 8 | z_hig << 16
        ones >>= 4
        zeros >>= 4
        if ones == 0xfffff and zeros == 0xfffff:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 20)

    def mbus_set_short_snoop_prefix(self, prefix=None):
        '''
        Set the short snoop prefix(es) of the ICE peripheral.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xf, 0xf)
        else:
            if len(prefix) != 4:
                raise self.FormatError, "Prefix must be exactly 4 bits"
            ones, zeros = self.string_to_masks(prefix)
        ones <<= 4
        zeros <<= 4
        self.send_message_until_acked('m', struct.pack("B"*(1+2),
            ord('S'),
            ones,
            zeros,
            ))

    def mbus_get_short_snoop_prefix(self):
        '''
        Get the short prefix(es) set for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('S')))
        if len(resp) != 2:
            raise self.FormatError, "Full prefix response should be 2 bytes"
        ones, zeros = struct.unpack("BB", resp)
        ones >>= 4
        zeros >>= 4
        if ones == 0xf and zeros == 0xf:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 4)

    def mbus_set_broadcast_channel_mask(self, mask=None):
        '''
        Set the broadcast mask for ICE board.

        The ICE board will report and ACK any messages sent to broadcast
        channels that match the mask set by this function. The special character 'x' is
        used to signify don't-care bits.

        Spaces are permitted and ignored. To disable this feature, set the
        address to None.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xf, 0xf)
        else:
            if len(prefix) != 4:
                raise self.FormatError, "Prefix must be exactly 4 bits"
            ones, zeros = self.string_to_masks(prefix)
        self.send_message_until_acked('m', struct.pack("B"*(1+2),
            ord('b'),
            ones,
            zeros,
            ))

    def mbus_get_broadcast_channel_mask(self):
        '''
        Get the broadcast mask for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('b')))
        if len(resp) != 2:
            raise self.FormatError, "Broadcast mask response should be 2 bytes"
        ones, zeros = struct.unpack("BB", resp)
        if ones == 0xf and zeros == 0xf:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 4)

    def mbus_set_broadcast_channel_snoop_mask(self, mask=None):
        '''
        Set the broadcast snoop mask for ICE board.

        The ICE board will report, but not ACK, any messages sent to broadcast
        channels that match the mask set by this function. The special character 'x' is
        used to signify don't-care bits.

        Spaces are permitted and ignored. To disable this feature, set the
        address to None.

        Default Value: DISABLED.
        '''
        if prefix is None:
            ones, zeros = (0xf, 0xf)
        else:
            if len(prefix) != 4:
                raise self.FormatError, "Prefix must be exactly 4 bits"
            ones, zeros = self.string_to_masks(prefix)
        self.send_message_until_acked('m', struct.pack("B"*(1+2),
            ord('B'),
            ones,
            zeros,
            ))

    def mbus_get_broadcast_channel_snoop_mask(self):
        '''
        Get the broadcast snoop mask for ICE.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('B')))
        if len(resp) != 2:
            raise self.FormatError, "Broadcast mask response should be 2 bytes"
        ones, zeros = struct.unpack("BB", resp)
        if ones == 0xf and zeros == 0xf:
            return None
        else:
            return self.masks_to_strings(ones, zeros, 4)

    def mbus_get_master_onoff(self):
        '''
        Get whether ICE is acting as MBus master node.
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('m')))
        if len(resp) != 1:
            raise self.FormatError, "Wrong response length from `Mm': " + str(resp)
        onoff = struct.unpack("B", resp)
        return bool(onoff)

    def mbus_set_master_onoff(self, onoff):
        '''
        Set whether ICE acts as MBus master node.

        DEFAULT: OFF
        '''
        msg = struct.pack("BB", ord('c'), onoff)
        self.send_message_until_acked('m', msg)

    def mbus_get_clock(self):
        '''
        Get ICE MBus clock speed. Only meaningful is ICE is MBus master.
        '''
        raise NotImplementedError
        #resp = self.send_message_until_acked('M', struct.pack("B", ord('c')))
        #if len(resp) != 1:
        #    raise self.FormatError, "Wrong response length from `Mc': " + str(resp)
        #onoff = struct.unpack("B", resp)
        #return bool(onoff)
        #return resp

    def mbus_set_clock(self, clock_speed):
        '''
        Set ICE MBus clock speed. Only meaningful is ICE is MBus master.

        DEFAULT: XXX
        '''
        #msg = struct.pack("BB", ord('c'), onoff)
        #self.send_message_until_acked('m', msg)
        raise NotImplementedError

    def mbus_get_should_interrupt(self):
        '''
        Get ICE MBus should interrupt setting.

        TODO: Fix interface (enums?)
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('i')))
        #if len(resp) != 1:
        #    raise self.FormatError, "Wrong response length from `Mc': " + str(resp)
        #onoff = struct.unpack("B", resp)
        #return bool(onoff)
        return resp

    def mbus_set_should_interrupt(self, should_interrupt):
        '''
        Set ICE MBus should interrupt setting.

        DEFAULT: Off
        '''
        msg = struct.pack("BB", ord('i'), onoff)
        self.send_message_until_acked('m', msg)

    def mbus_get_use_priority(self):
        '''
        Get ICE MBus use priority setting.

        TODO: Fix interface (enums?)
        '''
        resp = self.send_message_until_acked('M', struct.pack("B", ord('p')))
        #if len(resp) != 1:
        #    raise self.FormatError, "Wrong response length from `Mc': " + str(resp)
        #onoff = struct.unpack("B", resp)
        #return bool(onoff)
        return resp

    def mbus_set_use_priority(self, use_priority):
        '''
        Set ICE MBus use priority setting.

        DEFAULT: Off
        '''
        msg = struct.pack("BB", ord('p'), onoff)
        self.send_message_until_acked('m', msg)

    ## EIN DEBUG ##
    def ein_send(self, msg):
        '''
        Sends a message via the EIN Debug port.

        Takes a raw byte stream (e.g. "0xaa".decode('hex')).
        Returns the number of bytes actually sent.

        Long messages may be fragmented between the ICE library and the ICE
        FPGA. These fragments will be combined on the ICE board. There should be
        no interruption in message transmission.
        '''
        ret = self._fragment_sender('e', msg)
        return ret

    ## GPIO ##
    GPIO_INPUT = 0
    GPIO_OUTPUT = 1
    GPIO_TRISTATE = 2

    def gpio_get_level(self, gpio_idx):
        '''
        Query whether a gpio is high or low. (high=True)
        '''
        resp = self.send_message_until_acked('G',
                struct.pack('BB', ord('l'), gpio_idx))
        if len(resp) != 1:
            raise self.FormatError, "Too long of a response from `Gl#':" + str(resp)

        return bool(struct.unpack("B", resp))

    def gpio_get_direction(self, gpio_idx):
        '''
        Query gpio pin setup.

        Returns one of:
            ICE.GPIO_INPUT
            ICE.GPIO_OUTPUT
            ICE.GPIO_TRISTATE
        '''
        resp = self.send_message_until_acked('G',
                struct.pack('BB', ord('d'), gpio_idx))
        if len(resp) != 1:
            raise self.FormatError, "Too long of a response from `Gl#':" + str(resp)

        direction = struct.unpack("B", resp)
        if direction not in (ICE.GPIO_INPUT, ICE.GPIO_OUTPUT, ICE.GPIO_TRISTATE):
            raise self.FormatError, "Unknown direction: " + str(direction)

        return direction

    def gpio_set_level(self, gpio_idx, level):
        '''
        Set gpio level. (high=True)
        '''
        self.send_message_until_acked('g',
                struct.pack('BBB', ord('l'), gpio_idx, level))

    def gpio_set_direction(self, gpio_idx, direction):
        '''
        Setup a GPIO pin.
        '''
        if direction not in (ICE.GPIO_INPUT, ICE.GPIO_OUTPUT, ICE.GPIO_TRISTATE):
            raise self.ParameterError, "Unknown direction: " + str(direction)

        self.send_message_until_acked('g',
                struct.pack('BBB', ord('d'), gpio_idx, direction))

    ## POWER ##
    POWER_0P6 = 0
    POWER_1P2 = 1
    POWER_VBATT = 2

    POWER_0P6_DEFAULT = 0.675
    POWER_1P2_DEFAULT = 1.2
    POWER_VBATT_DEFAULT = 3.8

    def power_get_voltage(self, rail):
        '''
        Query the current voltage setting of a power rail.

        The `rail' argument must be one of:
            ICE.POWER_0P6
            ICE.POWER_1P2
            ICE.POWER_VBATT
        '''
        if rail not in (ICE.POWER_0P6, ICE.POWER_1P2, ICE.POWER_VBATT):
            raise self.ParameterError, "Invalid rail: " + str(rail)

        resp = self.send_message_until_acked('P', struct.pack("BB", ord('v'), rail))
        if len(resp) != 1:
            raise self.FormatError, "Too long of a response from `Pv#':" + str(resp)
        raw = struct.unpack("B", resp)

        # Vout = (0.537 + 0.0185 * v_set) * Vdefault
        default_voltage = (ICE.POWER_0P6_DEFAULT, ICE.POWER_1P2_DEFAULT,
                ICE.POWER_VBATT_DEFAULT)[rail]
        vout = (0.537 + 0.0185 * raw) * default_voltage
        return vout

    def power_get_onoff(self, rail):
        '''
        Query the current on/off setting of a power rail.

        Returns a boolean, on=True.
        '''
        if rail not in (ICE.POWER_0P6, ICE.POWER_1P2, ICE.POWER_VBATT):
            raise self.ParameterError, "Invalid rail: " + str(rail)

        resp = self.send_message_until_acked('P', struct.pack("BB", ord('o'), rail))
        if len(resp) != 1:
            raise self.FormatError, "Too long of a response from `Po#':" + str(resp)
        onoff = struct.unpack("B", resp)
        return bool(onoff)

    def power_set_voltage(self, rail, output_voltage):
        '''
        Set the voltage setting of a power rail. Units are V.
        '''
        if rail not in (ICE.POWER_0P6, ICE.POWER_1P2, ICE.POWER_VBATT):
            raise self.ParameterError, "Invalid rail: " + str(rail)

        # Vout = (0.537 + 0.0185 * v_set) * Vdefault
        output_voltage = float(output_voltage)
        default_voltage = (ICE.POWER_0P6_DEFAULT, ICE.POWER_1P2_DEFAULT,
                ICE.POWER_VBATT_DEFAULT)[rail]
        vset = ((output_voltage / default_voltage) - 0.537) / 0.0185
        vset = int(vset)
        if (vset < 0) or (vset > 255):
            raise self.ParameterError, "Voltage exceeds range. vset: " + str(vset)

        self.send_message_until_acked('p', struct.pack("BBB", ord('v'), rail, vset))

    def power_set_onoff(self, rail, onoff):
        '''
        Turn a power rail on or off (on=True).
        '''
        if rail not in (ICE.POWER_0P6, ICE.POWER_1P2, ICE.POWER_VBATT):
            raise self.ParameterError, "Invalid rail: " + str(rail)

        self.send_message_until_acked('p', struct.pack("BBB", ord('o'), rail, onoff))

if __name__ == '__main__':
    logger.setLevel(level=logging.DEBUG)