// autoprogrammer.ino
//
// this sketch allows an AVR to program another AVR containing one 
// of a number of different MCUs
//
// It is based on Optiloader.ino, which in turn was based on AVRISP
//
// Optiloader is Copyright (c) 2011, 2015 by Bill Westfield ("WestfW")
//

//-------------------------------------------------------------------------------------
// "MIT Open Source Software License":
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//-------------------------------------------------------------------------------------


#include <avr/pgmspace.h>
#include "autoprogrammer.h"
#include "images.h"

char Arduino_preprocessor_hint;

/*
 * Pins to target
 */
#define SCK 13
#define MISO 12
#define MOSI 11
#define RESET 10
#define POWER 9

// STK Definitions; we can still use these as return codes
#define STK_OK 0x10
#define STK_FAILED 0x11


// Useful message printing definitions
#define fp(string) flashprint(PSTR(string));
#define debug(string) // flashprint(PSTR(string));
#define error(string) flashprint(PSTR(string));

// Forward references
void pulse(int pin, int times);
void read_image(const image_t *ip);

// Global Variables

int pmode=0;
// address for reading and writing, set by 'U' command
int here;

uint16_t target_type = 0;		/* type of target_cpu */
uint16_t target_startaddr;
uint8_t target_pagesize;       /* Page size for flash programming (bytes) */
uint8_t *buff;

const image_t *target_flashptr; 	       /* pointer to target info in flash */
uint8_t target_code[512];	       /* The whole code */

void setup (void) {
  Serial.begin(19200); 			/* Initialize serial for status msgs */
  pinMode(13, OUTPUT); 			/* Blink the pin13 LED a few times */
  pulse(13,20);
}

void loop (void) {
  fp("\nOptiLoader Bootstrap programmer.\n2011 by Bill Westfield (WestfW)\n\n");
  if (target_poweron()) {		/* Turn on target power */
    do {
      if (!target_identify()) 		/* Figure out what kind of CPU */
        break;
      if (!target_findimage())		/* look for an image */
        break;
      if (!target_progfuses())		/* get fuses ready to program */
        break;
      if (!target_program()) 		/* Program the image */
        break;
      (void) target_normfuses(); 	/* reset fuses to normal mode */
    } 
    while (0);
  } 
  else {
    Serial.println();
  }
  target_poweroff(); 			/* turn power off */

  fp ("\nType 'G' or hit RESET for next chip\n")
    while (1) {
      if (Serial.read() == 'G')
        break;
    }
}

/*
 * Low level support functions
 */

/*
 * flashprint
 * print a text string direct from flash memory to Serial
 */
void flashprint (const char p[])
{
  uint8_t c;
  while (0 != (c = pgm_read_byte(p++))) {
    Serial.write(c);
  }
}

/*
 * hexton
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 */
uint8_t hexton (uint8_t h)
{
  if (h >= '0' && h <= '9')
    return(h - '0');
  if (h >= 'A' && h <= 'F')
    return((h - 'A') + 10);
  error("Bad hex digit!");
  return(0);
}

/*
 * pulse
 * turn a pin on and off a few times; indicates life via LED
 */
#define PTIME 30
void pulse (int pin, int times) {
  do {
    digitalWrite(pin, HIGH);
    delay(PTIME);
    digitalWrite(pin, LOW);
    delay(PTIME);
  } 
  while (times--);
}

/*
 * spi_init
 * initialize the AVR SPI peripheral
 */
void spi_init (void) {
  uint8_t x;
  SPCR = 0x53;  // SPIE | MSTR | SPR1 | SPR0
  x=SPSR;
  x=SPDR;
}

/*
 * spi_wait
 * wait for SPI transfer to complete
 */
void spi_wait (void) {
  debug("spi_wait");
  do {
  } 
  while (!(SPSR & (1 << SPIF)));
}

/*
 * spi_send
 * send a byte via SPI, wait for the transfer.
 */
uint8_t spi_send (uint8_t b) {
  uint8_t reply;
  SPDR=b;
  spi_wait();
  reply = SPDR;
  return reply;
}


/*
 * Functions specific to ISP programming of an AVR
 */

/*
 * target_identify
 * read the signature bytes (if possible) and check whether it's
 * a legal value (atmega8, atmega168, atmega328)
 */

boolean target_identify ()
{
  boolean result;
  target_type = 0;
  fp("\nReading signature:");
  target_type = read_signature();
  if (target_type == 0 || target_type == 0xFFFF) {
    fp(" Bad value: ");
    result = false;
  } 
  else {
    result = true;
  }
  Serial.println(target_type, HEX);
  if (target_type == 0) {
    fp("  (no target attached?)\n");
  }
  return result;
}

unsigned long spi_transaction (uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t n, m;
  spi_send(a); 
  n=spi_send(b);
  //if (n != a) error = -1;
  m=spi_send(c);
  return 0xFFFFFF & ((((uint32_t)n)<<16)+(m<<8) + spi_send(d));
}

uint16_t start_pmode () {
  uint16_t result;

  pinMode(13, INPUT); // restore to default
  spi_init();
  debug("...spi_init done");
  // following delays may not work on all targets...
  pinMode(RESET, OUTPUT);
  digitalWrite(RESET, HIGH);
  pinMode(SCK, OUTPUT);
  digitalWrite(SCK, LOW);
  delay(50);
  digitalWrite(RESET, LOW);
  delay(50);
  pinMode(MISO, INPUT);
  pinMode(MOSI, OUTPUT);
  debug("...spi_transaction");
  result = spi_transaction(0xAC, 0x53, 0x00, 0x00);
  debug("...Done");
  pmode = 1;
  return result;
}

void end_pmode (void) {
  SPCR = 0; 				/* reset SPI */
  digitalWrite(MISO, 0); 		/* Make sure pullups are off too */
  pinMode(MISO, INPUT);
  digitalWrite(MOSI, 0);
  pinMode(MOSI, INPUT);
  digitalWrite(SCK, 0);
  pinMode(SCK, INPUT);
  digitalWrite(RESET, 0);
  pinMode(RESET, INPUT);
  pmode = 0;
}

/*
 * read_image
 *
 * Read an intel hex image from a string in pgm memory.
 * We assume that the image does not exceed the 512 bytes that we have
 * allowed for it to have.  that would be bad.
 * Also read other data from the image, such as fuse and protecttion byte
 * values during programming, and for after we're done.
 */
void read_image (const image_t *ip)
{
  uint16_t len, totlen=0, addr;
  const char *hextext = &ip->image_hexcode[0];
  target_startaddr = 0;
  target_pagesize = pgm_read_byte(&ip->image_pagesize);
  uint8_t b, cksum = 0;

  while (1) {
    if (pgm_read_byte(hextext++) != ':') {
      error("No colon");
      break;
    }
    len = hexton(pgm_read_byte(hextext++));
    len = (len<<4) + hexton(pgm_read_byte(hextext++));
    cksum = len;

    b = hexton(pgm_read_byte(hextext++)); /* record type */
    b = (b<<4) + hexton(pgm_read_byte(hextext++));
    cksum += b;
    addr = b;
    b = hexton(pgm_read_byte(hextext++)); /* record type */
    b = (b<<4) + hexton(pgm_read_byte(hextext++));
    cksum += b;
    addr = (addr << 8) + b;
    if (target_startaddr == 0) {
      target_startaddr = addr;
      fp("  Start address at ");
      Serial.println(addr, HEX);
    } 
    else if (addr == 0) {
      break;
    }

    b = hexton(pgm_read_byte(hextext++)); /* record type */
    b = (b<<4) + hexton(pgm_read_byte(hextext++));
    cksum += b;

    for (uint8_t i=0; i < len; i++) {
      b = hexton(pgm_read_byte(hextext++));
      b = (b<<4) + hexton(pgm_read_byte(hextext++));
      if (addr - target_startaddr >= sizeof(target_code)) {
        error("Code extends beyond allowed range");
        break;
      }
      target_code[addr++ - target_startaddr] = b;
      cksum += b;
#if VERBOSE
      Serial.print(b, HEX);
      Serial.write(' ');
#endif
      totlen++;
      if (totlen >= sizeof(target_code)) {
        error("Too much code");
        break;
      }
    }
    b = hexton(pgm_read_byte(hextext++)); /* checksum */
    b = (b<<4) + hexton(pgm_read_byte(hextext++));
    cksum += b;
    if (cksum != 0) {
      error("Bad checksum: ");
      Serial.print(cksum, HEX);
    }
    if (pgm_read_byte(hextext++) != '\n') {
      error("No end of line");
      break;
    }
#if VERBOSE
    Serial.println();
#endif
  }
  fp("  Total bytes read: ");
  Serial.println(totlen);
}

/*
 * target_findimage
 *
 * given target_type loaded with the relevant part of the device signature,
 * search the hex images that we have programmed in flash, looking for one
 * that matches.
 */

boolean target_findimage ()
{
  const image_t *ip;
  fp("Searching for image...\n");
  /*
   * Search through our table of chip aliases first
   */
  for (uint8_t i=0; i < sizeof(aliases)/sizeof(aliases[0]); i++) {
    const alias_t *a = &aliases[i];
    if (a->real_chipsig == target_type) {
      fp("  Compatible bootloader for ");
      Serial.println(a->alias_chipname);
      target_type = a->alias_chipsig;  /* Overwrite chip signature */
      break;
    }
  }
  /*
   * Search through our table of self-contained images.
   */
  for (uint8_t i=0; i < sizeof(images)/sizeof(images[0]); i++) {
    target_flashptr = ip = images[i];
    if (ip && (pgm_read_word(&ip->image_chipsig) == target_type)) {
      fp("  Found \"");
      flashprint(&ip->image_name[0]);
      fp("\" for ");
      flashprint(&ip->image_chipname[0]);
      fp("\n");
      read_image(ip);
      return true;
    }
  }
  fp(" Not Found\n");
  return(false);
}

/*
 * target_progfuses
 * given initialized target image data, re-program the fuses to allow
 * the optiboot image to be programmed.
 */

boolean target_progfuses ()
{
  uint8_t f;
  fp("\nSetting fuses for programming");

  f = pgm_read_byte(&target_flashptr->image_progfuses[FUSE_PROT]);
  if (f) {
    fp("\n  Lock: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xE0, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_progfuses[FUSE_LOW]);
  if (f) {
    fp("  Low: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA0, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_progfuses[FUSE_HIGH]);
  if (f) {
    fp("  High: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA8, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_progfuses[FUSE_EXT]);
  if (f) {
    fp("  Ext: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA4, 0x00, f), HEX);
  }
  Serial.println();
  return true; 			/* */
}

/*
 * target_program
 * Actually program the image into the target chip
 */

boolean target_program ()
{
  int l; 				/* actual length */

  fp("\nProgramming bootloader: ");
  here = target_startaddr>>1; 		/* word address */
  buff = target_code;
  l = 512;
  Serial.print(l, DEC);
  fp(" bytes at 0x");
  Serial.println(here, HEX);

  spi_transaction(0xAC, 0x80, 0, 0); 	/* chip erase */
  delay(1000);
  if (write_flash(l) != STK_OK) {
    error("\nFlash Write Failed");
    return false;
  }
  return true; 			/*  */
}

/*
 * target_normfuses
 * reprogram the fuses to the state they should be in for bootloader
 * based programming
 */
boolean target_normfuses ()
{
  uint8_t f;
  fp("\nRestoring normal fuses");

  f = pgm_read_byte(&target_flashptr->image_normfuses[FUSE_PROT]);
  if (f) {
    fp("\n  Lock: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xE0, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_normfuses[FUSE_LOW]);
  if (f) {
    fp("  Low: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA0, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_normfuses[FUSE_HIGH]);
  if (f) {
    fp("  High: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA8, 0x00, f), HEX);
  }
  f = pgm_read_byte(&target_flashptr->image_normfuses[FUSE_EXT]);
  if (f) {
    fp("  Ext: ");
    Serial.print(f, HEX);
    fp(" ");
    Serial.print(spi_transaction(0xAC, 0xA4, 0x00, f), HEX);
  }
  Serial.println();
  return true; 			/* */
}

/*
 * target_poweron
 * Turn on power to the target chip (assuming that it is powered through
 * the relevant IO pin of THIS arduino.)
 */
boolean target_poweron ()
{
  uint16_t result;

  fp("Target power on! ...");
  digitalWrite(POWER, LOW);
  pinMode(POWER, OUTPUT);
  digitalWrite(POWER, HIGH);
  digitalWrite(RESET, LOW);  // reset it right away.
  pinMode(RESET, OUTPUT);
  /*
   * Check if the target is pulling RESET HIGH by reverting to input
   */
  delay(5);
  pinMode(RESET, INPUT);
  delay(1);
  if (digitalRead(RESET) != HIGH) {
    fp("No RESET pullup detected! - no target?");
    return false;
  }
  pinMode(RESET, OUTPUT);

  delay(200);
  fp("\nStarting Program Mode");
  result = start_pmode();
  if ((result & 0xFF00) != 0x5300) {
    fp(" - Failed, result = 0x");
    Serial.print(result, HEX);
    return false;
  }
  fp(" [OK]\n");
  return true;
}

boolean target_poweroff ()
{
  end_pmode();
  digitalWrite(POWER, LOW);
  delay(200);
  pinMode(POWER, INPUT);
  fp("\nTarget power OFF!\n");
  return true;
}

void flash (uint8_t hilo, int addr, uint8_t data) {
#if VERBOSE
  Serial.print(data, HEX);
  fp(":");
  Serial.print(spi_transaction(0x40+8*hilo, 
  addr>>8 & 0xFF, 
  addr & 0xFF,
  data), HEX);
  fp(" ");
#else
  (void) spi_transaction(0x40+8*hilo, 
  addr>>8 & 0xFF, 
  addr & 0xFF,
  data);
#endif
}

void commit (int addr) {
  fp("  Commit Page: ");
  Serial.print(addr, HEX);
  fp(":");
  Serial.println(spi_transaction(0x4C, (addr >> 8) & 0xFF, addr & 0xFF, 0), HEX);
  delay(100);
}

//#define _current_page(x) (here & 0xFFFFE0)
int current_page (int addr) {
  if (target_pagesize == 32) return here & 0xFFFFFFF0;
  if (target_pagesize == 64) return here & 0xFFFFFFE0;
  if (target_pagesize == 128) return here & 0xFFFFFFC0;
  return here;
}

uint8_t write_flash (int length) {
  if (target_pagesize < 1) return STK_FAILED;
  //if (target_pagesize != 64) return STK_FAILED;
  int page = current_page(here);
  int x = 0;
  while (x < length) {
    if (page != current_page(here)) {
      commit(page);
      page = current_page(here);
    }
    flash(LOW, here, buff[x]);
    flash(HIGH, here, buff[x+1]);
    x+=2;
    here++;
  }

  commit(page);

  return STK_OK;
}

uint16_t read_signature () {
  uint8_t sig_middle = spi_transaction(0x30, 0x00, 0x01, 0x00);
  uint8_t sig_low = spi_transaction(0x30, 0x00, 0x02, 0x00);
  return ((sig_middle << 8) + sig_low);
}