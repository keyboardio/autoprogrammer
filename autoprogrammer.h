#include <stdint.h>

typedef struct image {
    char image_chipname[12];	       /* ie "atmega168" */
    uint16_t image_chipsig;	       /* Low two bytes of signature */
    uint8_t image_progfuses[5];	       /* fuses to set during programming */
    uint8_t image_normfuses[5];	       /* fuses to set after programming */
    uint8_t image_pagesize;	       /* page size for flash programming */
    PGM_P image_hexcode_ptr;	       /* intel hex format image (text) */
} image_t;

typedef struct alias {
    char alias_chipname[12];		/* Name of chip.  ie Atmega168PA */
    uint16_t real_chipsig;		/* Low 16 bits actual chip sig. */
    uint16_t alias_chipsig;		/* "is the same as <otherchip sig>" */
} alias_t;

#define FUSE_PROT 0			/* memory protection */
#define FUSE_LOW 1			/* Low fuse */
#define FUSE_HIGH 2			/* High fuse */
#define FUSE_EXT 3			/* Extended fuse */

// Forward decl
extern const image_t PROGMEM image_tiny88, image_32u4;


// Forward references
void blink_led(int pin, int times);
void read_image(const image_t *ip);
uint8_t attempt_flash(void);
boolean target_identify (void);
boolean target_findimage (void);
boolean target_progfuses (void);
boolean target_program (void);
boolean target_normfuses (void);
boolean target_poweron (void);
boolean target_poweroff (void);


