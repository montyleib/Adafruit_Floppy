#include <Adafruit_Floppy.h>

#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
#define DENSITY_PIN  A0    // IDC 2
#define INDEX_PIN    A1    // IDC 8
#define SELECT_PIN   A2    // IDC 12
#define MOTOR_PIN    A3    // IDC 16
#define DIR_PIN      A4    // IDC 18
#define STEP_PIN     A5    // IDC 20
#define WRDATA_PIN   13    // IDC 22
#define WRGATE_PIN   12    // IDC 24
#define TRK0_PIN     11    // IDC 26
#define PROT_PIN     10    // IDC 28
#define READ_PIN      9    // IDC 30
#define SIDE_PIN      6    // IDC 32
#define READY_PIN     5    // IDC 34
#elif defined (ARDUINO_ADAFRUIT_FEATHER_RP2040)
#define DENSITY_PIN  A0    // IDC 2
#define INDEX_PIN    A1    // IDC 8
#define SELECT_PIN   A2    // IDC 12
#define MOTOR_PIN    A3    // IDC 16
#define DIR_PIN      24    // IDC 18
#define STEP_PIN     25    // IDC 20
#define WRDATA_PIN   13    // IDC 22
#define WRGATE_PIN   12    // IDC 24
#define TRK0_PIN     11    // IDC 26
#define PROT_PIN     10    // IDC 28
#define READ_PIN      9    // IDC 30
#define SIDE_PIN      8    // IDC 32
#define READY_PIN     7    // IDC 34
#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif
#elif defined (ARDUINO_RASPBERRY_PI_PICO)
#define DENSITY_PIN  2     // IDC 2
#define INDEX_PIN    3     // IDC 8
#define SELECT_PIN   4     // IDC 12
#define MOTOR_PIN    5     // IDC 16
#define DIR_PIN      6     // IDC 18
#define STEP_PIN     7     // IDC 20
#define WRDATA_PIN   8     // IDC 22 (not used during read)
#define WRGATE_PIN   9     // IDC 24 (not used during read)
#define TRK0_PIN    10     // IDC 26
#define PROT_PIN    11     // IDC 28
#define READ_PIN    12     // IDC 30
#define SIDE_PIN    13     // IDC 32
#define READY_PIN   14     // IDC 34
#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif
#else
#error "Please set up pin definitions!"
#endif

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

uint32_t time_stamp = 0;

uint8_t cmd_buffer[32], reply_buffer[128];
uint8_t cmd_buff_idx = 0;

#define GW_FIRMVER_MAJOR 1
#define GW_FIRMVER_MINOR 0
#define GW_MAXCMD      21
#define GW_HW_MODEL    8  // Adafruity
#define GW_HW_SUBMODEL 0  // Adafruit Floppy Generic
#define GW_USB_SPEED   0  // Full Speed

#define GW_CMD_GETINFO    0
#define GW_CMD_GETINFO_FIRMWARE 0
#define GW_CMD_GETINFO_BANDWIDTH 1
#define GW_CMD_SEEK       2
#define GW_CMD_HEAD       3
#define GW_CMD_SETPARAMS  4
#define GW_CMD_GETPARAMS  5
#define GW_CMD_GETPARAMS_DELAYS 0
#define GW_CMD_MOTOR      6
#define GW_CMD_READFLUX   7
#define GW_CMD_WRITEFLUX   8
#define GW_CMD_GETFLUXSTATUS  9
#define GW_CMD_SELECT    12
#define GW_CMD_DESELECT  13
#define GW_CMD_SETBUSTYPE 14
#define GW_CMD_SETBUSTYPE_IBM 1
#define GW_CMD_SETBUSTYPE_SHUGART 2
#define GW_CMD_SETPIN    15
#define GW_CMD_RESET     16
#define GW_CMD_SOURCEBYTES 18
#define GW_CMD_SINKBYTES 19
#define GW_CMD_GETPIN 20

#define GW_ACK_OK (byte)0
#define GW_ACK_BADCMD 1
#define GW_ACK_NOINDEX 2
#define GW_ACK_NOTRACK0 3
#define GW_ACK_NOUNIT 7

uint32_t timestamp = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  //while (!Serial) delay(100);
  Serial1.println("GrizzlyWizzly");

  floppy.debug_serial = &Serial1;
  if (!floppy.begin()) {
    Serial1.println("Failed to initialize floppy interface");
    while (1) yield();
  }
  timestamp = millis();
}

uint8_t get_cmd(uint8_t *buff, uint8_t maxbuff) {
  int i = 0;

  if (Serial.available() < 2) return 0;
  buff[i++] = Serial.read();
  buff[i++] = Serial.read();
  // wait for remaining data
  while (Serial.available() < (buff[1] - 2)) {
    delay(1);
    yield();
  }
  for (; i < buff[1]; i++) {
    buff[i] = Serial.read();
  }
  return i;
}

uint32_t bandwidth_timer;
float bytes_per_sec;
uint32_t transfered_bytes;
uint32_t captured_pulses;
// WARNING! there are 100K max flux pulses per track!
uint8_t flux_transitions[MAX_FLUX_PULSE_PER_TRACK];
bool motor_state = false; // we can cache whether the motor is spinning


void loop() {
  uint8_t cmd_len = get_cmd(cmd_buffer, sizeof(cmd_buffer));
  if (!cmd_len) {
    if ((millis() > timestamp) && ((millis() - timestamp) > 10000)) {
      Serial1.println("Timed out waiting for command, resetting motor");
      floppy.goto_track(0);
      floppy.spin_motor(false);
      motor_state = false;
      floppy.select(false);
      timestamp = millis();
    }
    return;
  }
  timestamp = millis();

  int i = 0;
  uint8_t cmd = cmd_buffer[0];
  memset(reply_buffer, 0, sizeof(reply_buffer));
  reply_buffer[i++] = cmd;  // echo back the cmd itself

  Serial1.printf("Got command 0x%02x of length %d\n\r", cmd, cmd_buffer[1]);

  if (cmd == GW_CMD_GETINFO) {
    Serial1.println("Get info");
    uint8_t sub_cmd = cmd_buffer[2];
    if (sub_cmd == GW_CMD_GETINFO_FIRMWARE) {
      reply_buffer[i++] = GW_ACK_OK;
      reply_buffer[i++] = GW_FIRMVER_MAJOR; // 1 byte
      reply_buffer[i++] = GW_FIRMVER_MINOR; // 1 byte
      reply_buffer[i++] = 1; // is main firm
      reply_buffer[i++] = GW_MAXCMD;
      uint32_t samplefreq = floppy.getSampleFrequency();
      reply_buffer[i++] = samplefreq & 0xFF;
      reply_buffer[i++] = (samplefreq >> 8) & 0xFF;
      reply_buffer[i++] = (samplefreq >> 16) & 0xFF;
      reply_buffer[i++] = (samplefreq >> 24) & 0xFF;
      reply_buffer[i++] = GW_HW_MODEL;
      reply_buffer[i++] = GW_HW_SUBMODEL;
      reply_buffer[i++] = GW_USB_SPEED;
      Serial.write(reply_buffer, 34);
    }
    else if (sub_cmd == GW_CMD_GETINFO_BANDWIDTH) {
      reply_buffer[i++] = GW_ACK_OK;
      uint32_t min_bytes = transfered_bytes;
      uint32_t max_bytes = transfered_bytes;
      uint32_t min_usec =  bandwidth_timer * 1000;
      uint32_t max_usec =  bandwidth_timer * 1000;
      // TODO What is this math supposed to be??

      reply_buffer[i++] = min_bytes & 0xFF;
      reply_buffer[i++] = min_bytes >> 8;
      reply_buffer[i++] = min_bytes >> 16;
      reply_buffer[i++] = min_bytes >> 24;
      reply_buffer[i++] = min_usec & 0xFF;
      reply_buffer[i++] = min_usec >> 8;
      reply_buffer[i++] = min_usec >> 16;
      reply_buffer[i++] = min_usec >> 24;
      reply_buffer[i++] = max_bytes & 0xFF;
      reply_buffer[i++] = max_bytes >> 8;
      reply_buffer[i++] = max_bytes >> 16;
      reply_buffer[i++] = max_bytes >> 24;
      reply_buffer[i++] = max_usec & 0xFF;
      reply_buffer[i++] = max_usec >> 8;
      reply_buffer[i++] = max_usec >> 16;
      reply_buffer[i++] = max_usec >> 24;

      // TODO more?
      Serial.write(reply_buffer, 34);
    }
  }

  else if (cmd == GW_CMD_GETPARAMS) {
    Serial1.println("Get params");
    uint8_t sub_cmd = cmd_buffer[2];
    if (sub_cmd == GW_CMD_GETPARAMS_DELAYS) {
      reply_buffer[i++] = GW_ACK_OK;
      reply_buffer[i++] = floppy.select_delay_us & 0xFF;
      reply_buffer[i++] = floppy.select_delay_us >> 8;
      reply_buffer[i++] = floppy.step_delay_us & 0xFF;
      reply_buffer[i++] = floppy.step_delay_us >> 8;
      reply_buffer[i++] = floppy.settle_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.settle_delay_ms >> 8;
      reply_buffer[i++] = floppy.motor_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.motor_delay_ms >> 8;
      reply_buffer[i++] = floppy.watchdog_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.watchdog_delay_ms >> 8;
      Serial.write(reply_buffer, 12);
    }
  }

  else if (cmd == GW_CMD_RESET) {
    Serial1.println("Soft reset");
    floppy.soft_reset();
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_SETBUSTYPE) {
    uint8_t bustype = cmd_buffer[2];
    Serial1.printf("Set bus type %d\n\r", bustype);
    // TODO: whats the diff???
    if (bustype == GW_CMD_SETBUSTYPE_IBM) {
      reply_buffer[i++] = GW_ACK_OK;
    }
    else if (bustype == GW_CMD_SETBUSTYPE_SHUGART) {
      floppy.bus_type = BUSTYPE_SHUGART;
      reply_buffer[i++] = GW_ACK_OK;
    } else {
      reply_buffer[i++] = GW_ACK_BADCMD;
    }
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_SEEK) {
    uint8_t track = cmd_buffer[2];
    Serial1.printf("Seek track %d\n\r", track);
    bool r = floppy.goto_track(track);
    if (r) {
      reply_buffer[i++] = GW_ACK_OK;
    } else {
      reply_buffer[i++] = GW_ACK_NOTRACK0;
    }
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_HEAD) {
    uint8_t head = cmd_buffer[2];
    Serial1.printf("Seek head %d\n\r", head);
    floppy.side(head);
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_MOTOR) {
    uint8_t unit = cmd_buffer[2];
    uint8_t state = cmd_buffer[3];
    Serial1.printf("Turn motor %d %s\n\r", unit, state ? "on" : "off");
    if (motor_state != state) { // we're in the opposite state
      if (! floppy.spin_motor(state)) {
        reply_buffer[i++] = GW_ACK_NOINDEX;
      } else {
        reply_buffer[i++] = GW_ACK_OK;
      }
      motor_state = state;
    } else {
      // our cached state is correct!
      reply_buffer[i++] = GW_ACK_OK;
    }
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_SELECT) {
    uint8_t sub_cmd = cmd_buffer[2];
    Serial1.printf("Select drive %d\n\r", sub_cmd);
    if (sub_cmd == 0) {
      floppy.select(true);
      reply_buffer[i++] = GW_ACK_OK;
    } else {
      reply_buffer[i++] = GW_ACK_NOUNIT;
    }
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_DESELECT) {
    Serial1.printf("Deselect drive\n\r");
    floppy.select(false);
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_READFLUX) {
    uint32_t flux_ticks;
    uint16_t revs;
    flux_ticks = cmd_buffer[5];
    flux_ticks <<= 8;
    flux_ticks |= cmd_buffer[4];
    flux_ticks <<= 8;
    flux_ticks |= cmd_buffer[3];
    flux_ticks <<= 8;
    flux_ticks |= cmd_buffer[2];
    revs = cmd_buffer[7];
    revs <<= 8;
    revs |= cmd_buffer[6];
    if (revs) {
      revs -= 1;
    }

    if (floppy.track() == -1) {
      floppy.goto_track(0);
    }

    Serial1.printf("Reading flux0rs on track %d: %u ticks and %d revs\n\r", floppy.track(), flux_ticks, revs);
    uint16_t capture_ms = 0;
    uint16_t capture_revs = 0;
    if (flux_ticks) {
      // we'll back calculate the revolutions
      capture_ms = 1000.0 * (float)flux_ticks / (float)floppy.getSampleFrequency();
      revs = 1;
    } else {
      capture_revs = revs;
      capture_ms = 0;
    }

    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
    while (revs--) {
      uint32_t index_offset;
      // read in greaseweazle mode (long pulses encoded with 250's)
      captured_pulses = floppy.capture_track(flux_transitions, sizeof(flux_transitions),
                                             &index_offset, true, capture_ms);
      Serial1.printf("Rev #%d captured %u pulses, second index fall @ %d\n\r",
                     revs, captured_pulses, index_offset);
      //floppy.print_pulse_bins(flux_transitions, captured_pulses, 64, Serial1);
      // Send the index falling signal opcode, which was right
      // at the start of this data xfer (we wait for index to fall
      // before we start reading
      reply_buffer[0] = 0xFF; // FLUXOP INDEX
      reply_buffer[1] = 1; // index opcode
      reply_buffer[2] = 0x1;  // 0 are special, so we send 1's to == 0
      reply_buffer[3] = 0x1;  // ""
      reply_buffer[4] = 0x1;  // ""
      reply_buffer[5] = 0x1;  // ""
      Serial.write(reply_buffer, 6);

      uint8_t *flux_ptr = flux_transitions;
      // send all data until the flux transition
      while (index_offset) {
        uint32_t to_send = min(index_offset, (uint32_t)256);
        Serial.write(flux_ptr, to_send);
        //Serial1.println(to_send);
        flux_ptr += to_send;
        captured_pulses -= to_send;
        index_offset -= to_send;
      }

      // there's more flux to follow this, so we need an index fluxop
      if (!revs || capture_ms) {
        // we interrupt this broadcast for a flux op index
        reply_buffer[0] = 0xFF; // FLUXOP INDEX
        reply_buffer[1] = 1; // index opcode
        reply_buffer[2] = 0x1;  // 0 are special, so we send 1's to == 0
        reply_buffer[3] = 0x1;  // ""
        reply_buffer[4] = 0x1;  // ""
        reply_buffer[5] = 0x1;  // ""
        Serial.write(reply_buffer, 6);
      }

      // send remaining data until the flux transition
      if (capture_ms) {
        while (captured_pulses) {
          uint32_t to_send = min(captured_pulses, (uint32_t)256);
          Serial.write(flux_ptr, to_send);
          //Serial1.println(to_send);
          flux_ptr += to_send;
          captured_pulses -= to_send;
        }
      }
    }
    // flush input, to account for fluxengine bug
    while (Serial.available()) Serial.read();

    // THE END
    Serial.write((byte)0);
  }
  else if (cmd == GW_CMD_WRITEFLUX) {
    Serial1.println("write flux");

    //uint8_t cue_at_index = cmd_buffer[2];
    //uint8_t terminate_at_index = cmd_buffer[3];
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);

    uint32_t fluxors = 0;
    uint8_t flux = 0xFF;
    while (flux && (fluxors < MAX_FLUX_PULSE_PER_TRACK)) {
      while (!Serial.available()) yield();
      flux = Serial.read();
      flux_transitions[fluxors++] = flux;
    }
    if (fluxors == MAX_FLUX_PULSE_PER_TRACK) {
      Serial1.println("*** FLUX OVERRUN ***");
      while (1) yield();
    }
    floppy.write_track(flux_transitions, fluxors - 7, true);
    Serial1.println("wrote fluxors");
    Serial.write((byte)0);
  }
  else if (cmd == GW_CMD_GETFLUXSTATUS) {
    Serial1.println("get flux status");
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
  }


  else if (cmd == GW_CMD_SINKBYTES) {
    uint32_t numbytes = 0;
    uint32_t seed = 0;
    numbytes |= cmd_buffer[5];
    numbytes <<= 8;
    numbytes |= cmd_buffer[4];
    numbytes <<= 8;
    numbytes |= cmd_buffer[3];
    numbytes <<= 8;
    numbytes |= cmd_buffer[2];
    Serial1.printf("sink numbytes %d\n\r", numbytes);

    seed |= cmd_buffer[9];
    seed <<= 8;
    seed |= cmd_buffer[8];
    seed <<= 8;
    seed |= cmd_buffer[7];
    seed <<= 8;
    seed |= cmd_buffer[6];
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
    yield();
    bandwidth_timer = millis();
    transfered_bytes = numbytes;
    bytes_per_sec = numbytes;

    while (numbytes != 0) {
      uint32_t avail = Serial.available();
      if (avail == 0) {
        //Serial1.print("-");
        yield();
        continue;
      }
      //Serial1.printf("%lu avail, ", avail);
      uint32_t to_read = min(numbytes, min((uint32_t)sizeof(reply_buffer), avail));
      //Serial1.printf("%lu to read, ", to_read);
      numbytes -= Serial.readBytes((char *)reply_buffer, to_read);
      //Serial1.printf("%lu remain\n\r", numbytes);
    }
    bandwidth_timer = millis() - bandwidth_timer;
    bytes_per_sec /= bandwidth_timer;
    bytes_per_sec *= 1000;
    Serial1.print("Done in ");
    Serial1.print(bandwidth_timer);
    Serial1.print(" ms, ");
    Serial1.print(bytes_per_sec);
    Serial1.println(" bytes per sec");
    Serial.write(GW_ACK_OK);
    yield();
  }
  else if (cmd == GW_CMD_SOURCEBYTES) {
    uint32_t numbytes = 0;
    uint32_t seed = 0;
    numbytes |= cmd_buffer[5];
    numbytes <<= 8;
    numbytes |= cmd_buffer[4];
    numbytes <<= 8;
    numbytes |= cmd_buffer[3];
    numbytes <<= 8;
    numbytes |= cmd_buffer[2];
    Serial1.printf("source numbytes %d\n\r", numbytes);

    seed |= cmd_buffer[9];
    seed <<= 8;
    seed |= cmd_buffer[8];
    seed <<= 8;
    seed |= cmd_buffer[7];
    seed <<= 8;
    seed |= cmd_buffer[6];
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
    yield();
    bandwidth_timer = millis();
    bytes_per_sec = numbytes;
    transfered_bytes = numbytes;

    uint32_t randnum = seed;
    while (numbytes != 0) {
      uint32_t to_write = min(numbytes, sizeof(reply_buffer));
      // we dont write 'just anything'!
      for (uint32_t i = 0; i < to_write; i++) {
        reply_buffer[i] = randnum;
        if (randnum & 0x01) {
          randnum = (randnum >> 1) ^ 0x80000062;
        } else {
          randnum >>= 1;
        }
      }
      numbytes -= Serial.write(reply_buffer, to_write);
    }
    bandwidth_timer = millis() - bandwidth_timer;
    bytes_per_sec /= bandwidth_timer;
    bytes_per_sec *= 1000;
    Serial1.print("Done in ");
    Serial1.print(bandwidth_timer);
    Serial1.print(" ms, ");
    Serial1.print(bytes_per_sec);
    Serial1.println(" bytes per sec");
  } else if (cmd == GW_CMD_GETPIN) {
    uint32_t pin = cmd_buffer[2];
    Serial1.printf("getpin %d\n\r", pin);

    switch (pin) {
      case 26:
        reply_buffer[i++] = GW_ACK_OK;
        reply_buffer[i++] = digitalRead(TRK0_PIN);
        break;

      default:
        // unknown pin, don't pretend we did it right
        reply_buffer[i++] = GW_ACK_BADCMD;
        reply_buffer[i++] = 0;
    }
    Serial.write(reply_buffer, i);
  } else if (cmd == GW_CMD_SETPIN) {
    uint32_t pin = cmd_buffer[2];
    bool value = cmd_buffer[3];
    Serial1.printf("setpin %d to \n\r", pin, value);

    switch (pin) {
      case 2:
        pinMode(DENSITY_PIN, OUTPUT);
        digitalWrite(DENSITY_PIN, value);
        reply_buffer[i++] = GW_ACK_OK;
        break;

      default:
        // unknown pin, don't pretend we did it right
        reply_buffer[i++] = GW_ACK_BADCMD;
    }

    Serial.write(reply_buffer, i);

    /********** unknown ! ********/
  } else {
    reply_buffer[i++] = GW_ACK_BADCMD;
    Serial.write(reply_buffer, 2);
  }
  //Serial1.println("cmd complete!");
}
