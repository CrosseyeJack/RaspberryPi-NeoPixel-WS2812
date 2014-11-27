// Set tabs to 4 spaces.

// =================================================================================================
//
//		 __      __  _________________   ______  ____________   ____________________.__ 
//		/  \    /  \/   _____/\_____  \ /  __  \/_   \_____  \  \______   \______   \__|
//		\   \/\/   /\_____  \  /  ____/ >      < |   |/  ____/   |       _/|     ___/  |
//		 \        / /        \/       \/   --   \|   /       \   |    |   \|    |   |  |
//		  \__/\  / /_______  /\_______ \______  /|___\_______ \  |____|_  /|____|   |__|
//		       \/          \/         \/      \/             \/         \/              
//
// WS2812 NeoPixel driver
// Based on code by Richard G. Hirst and others
// Adapted for the WS2812 by 626Pilot, April/May 2014
// Huge ASCII art section labels are from http://patorjk.com/software/taag/
//
// License: GPL
//
// You are using this at your OWN RISK. I believe this software is reasonably safe to use (aside
// from the intrinsic risk to those who are photosensitive - see below), although I can't be certain
// that it won't trash your hardware or cause property damage.
//
// Speaking of risk, WS2812 pixels are bright enough to cause eye pain and (for all I know) possibly
// retina damage when run at full strength. It's a good idea to set the brightness at 0.2 or so for
// direct viewing (whether you're looking directly at the pixels or not), or to put some diffuse
// material between you and the LEDs.
//
// PHOTOSENSITIVITY WARNING:
// Patterns of light and darkness (stationary or moving), flashing lights, patterns and backgrounds
// on screens, and the like, may cause epilleptic seizures in some people. This is a danger EVEN IF
// THE PERSON (WHICH MAY BE *YOU*) HAS NEVER KNOWINGLY HAD A PHOTOSENSITIVE EPISODE BEFORE. It's up
// to you to learn the warning signs, but symptoms may include dizziness, nausea, vision changes,
// convlusions, disorientation, involuntary movements, and eye twitching. (This list is not
// necessarily exhaustive.)
//
// NEOPIXEL BEST PRACTICES: https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices
//
// Connections:
//		Positive to Raspberry Pi's 3.3v
//		Negative to Raspberry Pi's ground
// 		Data to GPIO18 (Pin 12) (through a resistor, which you should know from the Best
// 		Practices guide!)
//
// GitHub (source, support, etc.): https://github.com/626Pilot/RaspberryPi-NeoPixel-WS2812
//    Buy WS2812-based stuff from: http://adafruit.com
//                   Compile with: gcc ws2812-RPi.c -o ws2812-RPi
//                      Test with: sudo ./ws2812-RPi
//                                 (it needs to be root so it can map the peripherals' registers)
//
// =================================================================================================

// This is for the WS2812 LEDs. It won't work with the older WS2811s, although it could be modified
// for that without too much trouble. Preliminary driver used Frank Buss' servo driver, but I moved
// to Richard Hirst's memory mapping/access model because his code already works with DMA, and has
// what I think is a slightly cleaner way of accessing the registers: register[name] rather than
// *(register + name).

// At the time of writing, there's a lot of confusing "PWM DMA" code revolving around simulating
// an FM signal. Usually this is done without properly initializing certain registers, which is
// OK for their purpose, but I needed to be able to transfer actual coherent data and have it wind
// up in a proper state once it was transferred. This has proven to be a somewhat painful task.
// The PWM controller likes to ignore the RPTL1 bit when the data is in a regular, repeating
// pattern. I'M NOT MAKING IT UP! It really does that. It's bizarre. There are lots of other
// strange irregularities as well, which had to be figured out through trial and error. It doesn't
// help that the BCM2835 ARM Peripherals manual contains outright errors and omissions!

// Many examples of this kind of code have magic numbers in them. If you don't know, a magic number
// is one that either lacks an obvious structure (e.g. 0x2020C000) or purpose. Please don't use
// that stuff in any code you release! All magic numbers found in reference code have been changed
// to DEFINEs. That way, instead of seeing some inscrutable number, you see (e.g.) PWM_CTL.

// References - BCM2835 ARM Peripherals:
//              http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
//
//              Raspberry Pi low-level peripherals:
//              http://elinux.org/RPi_Low-level_peripherals
//
//				Richard Hirst's nice, clean code:
//				https://github.com/richardghirst/PiBits/blob/master/PiFmDma/PiFmDma.c
//
//              PWM clock register:
//              http://www.raspberrypi.org/forums/viewtopic.php?t=8467&p=124620
//
//				Simple (because it's in assembly) PWM+DMA setup:
//				https://github.com/mikedurso/rpi-projects/blob/master/asm-nyancat/rpi-nyancat.s
//
//				Adafruit's NeoPixel driver:
//				https://github.com/adafruit/Adafruit_NeoPixel/blob/master/Adafruit_NeoPixel.cpp

#include "ws2812-RPi.h"

// =================================================================================================
//	  ________                                  .__   
//	 /  _____/  ____   ____   ________________  |  |  
//	/   \  ____/ __ \ /    \_/ __ \_  __ \__  \ |  |  
//	\    \_\  \  ___/|   |  \  ___/|  | \// __ \|  |__
//	 \______  /\___  >___|  /\___  >__|  (____  /____/
//	        \/     \/     \/     \/           \/      
// =================================================================================================

// Convenience functions
// --------------------------------------------------------------------------------------------------
// Print some bits of a binary number (2nd arg is how many bits)
void printBinary(unsigned int i, unsigned int bits) {
	int x;
	for(x=bits-1; x>=0; x--) {
		printf("%d", (i & (1 << x)) ? 1 : 0);
		if(x % 16 == 0 && x > 0) {
			printf(" ");
		} else if(x % 4 == 0 && x > 0) {
			printf(":");
		}
	}
}

// Reverse the bits in a word
unsigned int reverseWord(unsigned int word) {
	unsigned int output = 0;
	unsigned char bit;
	int i;
	for(i=0; i<32; i++) {
		bit = word & (1 << i) ? 1 : 0;
		output |= word & (1 << i) ? 1 : 0;
		if(i<31) {
			output <<= 1;
		}
	}
	return output;
}

// Not sure how this is better than usleep...?
/*
static void udelay(int us) {
	struct timespec ts = { 0, us * 1000 };
	nanosleep(&ts, NULL);
}
*/


// Shutdown functions
// --------------------------------------------------------------------------------------------------
static void terminate(int dummy) {
	// zero out all the LED's
	int i;
	for (i=0; i < numPixels(); i++) {
		setPixelColor(i, 0, 0, 0);
	}
	show();


	// Shut down the DMA controller
	if(dma_reg) {
		CLRBIT(dma_reg[DMA_CS], DMA_CS_ACTIVE);
		usleep(100);
		SETBIT(dma_reg[DMA_CS], DMA_CS_RESET);
		usleep(100);
	}

	// Shut down PWM
	if(pwm_reg) {
		CLRBIT(pwm_reg[PWM_CTL], PWM_CTL_PWEN1);
		usleep(100);
		pwm_reg[PWM_CTL] = (1 << PWM_CTL_CLRF1);
	}
	
	// Free the allocated memory
	if(page_map != 0) {
		free(page_map);
	}

	exit(1);
}

static void fatal(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	terminate(0);
}


// Memory management
// --------------------------------------------------------------------------------------------------
// Translate from virtual address to physical
static unsigned int mem_virt_to_phys(void *virt) {
	unsigned int offset = (uint8_t *)virt - virtbase;
	return page_map[offset >> PAGE_SHIFT].physaddr + (offset % PAGE_SIZE);
}

// Translate from physical address to virtual
static unsigned int mem_phys_to_virt(uint32_t phys) {
	unsigned int pg_offset = phys & (PAGE_SIZE - 1);
	unsigned int pg_addr = phys - pg_offset;
	int i;

	for (i = 0; i < NUM_PAGES; i++) {
		if (page_map[i].physaddr == pg_addr) {
			return (uint32_t)virtbase + i * PAGE_SIZE + pg_offset;
		}
	}
	fatal("Failed to reverse map phys addr %08x\n", phys);

	return 0;
}

// Map a peripheral's IO memory into our virtual memory, so we can read/write it directly
static void * map_peripheral(uint32_t base, uint32_t len) {
	int fd = open("/dev/mem", O_RDWR);
	void * vaddr;

	if (fd < 0) {
		// Got a segmentation fault when not running as root
		// So instead of using fatal(); I'm going to print the error (and a hint) and then exit();
		printf("Failed to open /dev/mem: %m (try running as root)\n");
		exit(EXIT_FAILURE);
	}
	vaddr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, base);
	if (vaddr == MAP_FAILED)
		fatal("Failed to map peripheral at 0x%08x: %m\n", base);
	close(fd);

	return vaddr;
}


// =================================================================================================
//	.____     ___________________      _________ __          _____  _____ 
//	|    |    \_   _____/\______ \    /   _____//  |_ __ ___/ ____\/ ____\
//	|    |     |    __)_  |    |  \   \_____  \\   __\  |  \   __\\   __\ 
//	|    |___  |        \ |    `   \  /        \|  | |  |  /|  |   |  |   
//	|_______ \/_______  //_______  / /_______  /|__| |____/ |__|   |__|   
//	        \/        \/         \/          \/                           
// =================================================================================================

// Set brightness
unsigned char setBrightness(float b) {
	if(b < 0) {
		printf("Brightness can't be set below 0.\n");
		return false;
	}
	if(b > 1) {
		printf("Brightness can't be set above 1.\n");
		return false;
	}
	brightness = b;
	return true;
}

// Zero out the PWM waveform buffer
void clearPWMBuffer() {
	memset(PWMWaveform, 0, NUM_DATA_WORDS * 4);	// Times four because memset deals in bytes.
}

// Zero out the LED buffer
void clearLEDBuffer() {
	int i;
	for(i=0; i<NUM_PIXELS; i++) {
		LEDBuffer[i].r = 0;
		LEDBuffer[i].g = 0;
		LEDBuffer[i].b = 0;
	}
}

// Turn r, g, and b into a Color_t struct
Color_t RGB2Color(unsigned char r, unsigned char g, unsigned char b) {
	Color_t color = { r, g, b };
	return color;
	//return ((unsigned int)r << 16) | ((unsigned int)g << 8) | b;
}

// Alias for the above
Color_t Color(unsigned char r, unsigned char g, unsigned char b) {
	return RGB2Color(r, g, b);
}

// Set pixel color (24-bit color)
unsigned char setPixelColor(unsigned int pixel, unsigned char r, unsigned char g, unsigned char b) {
	if(pixel < 0) {
		printf("Unable to set pixel %d (less than zero?)\n", pixel);
		return false;
	}
	// // Lets break things and comment out this
	// if(pixel > NUM_PIXELS - 1) {
	// 	printf("Unable to set pixel %d (LED buffer is %d pixels long)\n", pixel, NUM_PIXELS);
	// 	return false;
	// }
	LEDBuffer[pixel] = RGB2Color(r, g, b);
	return true;
}

// Set pixel color, by a direct Color_t
unsigned char setPixelColorT(unsigned int pixel, Color_t c) {
	if(pixel < 0) {
		printf("Unable to set pixel %d (less than zero?)\n", pixel);
		return false;
	}
	// if(pixel > NUM_PIXELS - 1) {
	// 	printf("Unable to set pixel %d (LED buffer is %d pixels long)\n", pixel, NUM_PIXELS);
	// 	return false;
	// }
	LEDBuffer[pixel] = c;
	return true;
}

// Get pixel color
Color_t getPixelColor(unsigned int pixel) {
	if(pixel < 0) {
		printf("Unable to get pixel %d (less than zero?)\n", pixel);
		return RGB2Color(0, 0, 0);
	}
	// if(pixel > NUM_PIXELS - 1) {
	// 	printf("Unable to get pixel %d (LED buffer is %d pixels long)\n", pixel, NUM_PIXELS);
	// 	return RGB2Color(0, 0, 0);
	// }
	return LEDBuffer[pixel];
}

// Return # of pixels
unsigned int numPixels() {
	return numLEDs;
}

// Return pointer to pixels (FIXME: dunno if this works!)
Color_t* getPixels() {
	return LEDBuffer;
}

// Set an individual bit in the PWM output array, accounting for word boundaries
// The (31 - bitIdx) is so that we write the data backwards, correcting its endianness
// This means getPWMBit will return something other than what was written, so it would be nice
// if the logic that calls this function would figure it out instead. (However, that's trickier)
void setPWMBit(unsigned int bitPos, unsigned char bit) {

	// Fetch word the bit is in
	unsigned int wordOffset = (int)(bitPos / 32);
	unsigned int bitIdx = bitPos - (wordOffset * 32);

//	printf("bitPos=%d wordOffset=%d bitIdx=%d value=%d\n", bitPos, wordOffset, bitIdx, bit);

	switch(bit) {
		case 1:
			PWMWaveform[wordOffset] |= (1 << (31 - bitIdx));
//			PWMWaveform[wordOffset] |= (1 << bitIdx);
			break;
		case 0:
			PWMWaveform[wordOffset] &= ~(1 << (31 - bitIdx));
//			PWMWaveform[wordOffset] &= ~(1 << bitIdx);
			break;
	}
}

// Get an individual bit from the PWM output array, accounting for word boundaries
unsigned char getPWMBit(unsigned int bitPos) {

	// Fetch word the bit is in
	unsigned int wordOffset = (int)(bitPos / 32);
	unsigned int bitIdx = bitPos - (wordOffset * 32);

	if(PWMWaveform[wordOffset] & (1 << bitIdx)) {
		return true;
	} else {
		return false;
	}
}



// =================================================================================================
//	________        ___.
//	\______ \   ____\_ |__  __ __  ____  
//	 |    |  \_/ __ \| __ \|  |  \/ ___\ 
//	 |    `   \  ___/| \_\ \  |  / /_/  >
//	 /_______  /\___  >___  /____/\___  / 
//	         \/     \/    \/     /_____/  
// =================================================================================================

// Dump contents of LED buffer
void dumpLEDBuffer() {
	int i;
	printf("Dumping LED buffer:\n");
	for(i=0; i<NUM_PIXELS; i++) {
		printf("R:%X G:%X B:%X\n", LEDBuffer[i].r, LEDBuffer[i].g, LEDBuffer[i].b);
	}
}

// Dump contents of PWM waveform
// The last number dumped may not have a multiple of 3 digits (our basic unit of data is 3 bits,
// whereas the RAM comprising the buffer has to be a multiple of 2 bits in size)
void dumpPWMBuffer() {
	int i;
	printf("Dumping PWM output buffer:\n");
	for(i = 0; i < NUM_DATA_WORDS * 32; i++) {
		printf("%d", getPWMBit(i));
		if(i != 0 && i % 72 == 71) {
			printf("\n");
		} else if(i % 3 == 2) {
			if(i % 8 == 7) {
				printf(" ");
			} else {
				printf(":");
			}
		}
	}
	printf("\n");
}

// Display the status of the PWM's control register
void dumpPWMStatus() {
	printf("PWM Status Register\n");
	printf("    FULL1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_FULL1) ? 1 : 0);
	printf("    EMPT1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_EMPT1) ? 1 : 0);
	printf("    WERR1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_WERR1) ? 1 : 0);
	printf("    RERR1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_RERR1) ? 1 : 0);
	printf("    GAPO1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_GAPO1) ? 1 : 0);
	printf("     BERR: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_BERR) ? 1 : 0);
	printf("     STA1: %d\n", pwm_reg[PWM_STA] & (1 << PWM_STA_STA1) ? 1 : 0);
	printf("\n");
}

// Display the settings in a PWM control word
// If you want to dump the register directly, use this: dumpPWMControl(*(pwm + PWM_CTL));
void dumpPWMControl(unsigned int word) {
	printf("PWM Control Register\n");
	printf("    PWEN1: %d\n", word & (1 << PWM_CTL_PWEN1) ? 1 : 0);
	printf("    MODE1: %d\n", word & (1 << PWM_CTL_MODE1) ? 1 : 0);
	printf("    RPTL1: %d\n", word & (1 << PWM_CTL_RPTL1) ? 1 : 0);
	printf("    SBIT1: %d\n", word & (1 << PWM_CTL_SBIT1) ? 1 : 0);
	printf("    POLA1: %d\n", word & (1 << PWM_CTL_POLA1) ? 1 : 0);
	printf("    USEF1: %d\n", word & (1 << PWM_CTL_USEF1) ? 1 : 0);
	printf("    CLRF1: %d\n", word & (1 << PWM_CTL_CLRF1) ? 1 : 0);
	printf("    MSEN1: %d\n", word & (1 << PWM_CTL_MSEN1) ? 1 : 0);
	printf("\n");
}

// Display the settings in the PWM DMAC word
void dumpPWMDMAC() {
	printf("PWM DMAC Register\n");
	printf("     ENAB: %d\n", GETBIT(pwm_reg[PWM_DMAC], PWM_DMAC_ENAB));
	printf("    PANIC: %d\n", (pwm_reg[PWM_DMAC] >> PWM_DMAC_PANIC) & 0b11111111);
	printf("     DREQ: %d\n", (pwm_reg[PWM_DMAC] >> PWM_DMAC_DREQ) & 0b11111111);
	printf("\n");
}

// Display all PWM registers
void dumpPWM() {
	dumpPWMStatus();
	dumpPWMControl(pwm_reg[PWM_CTL]);
	dumpPWMDMAC();
}

// Display all PWM control registers
void dumpDMARegs() {
	printf("DMA Registers\n");
	printf("	     CONBLK_AD: 0x%x (", dma_reg[DMA_CONBLK_AD]);
	printBinary(dma_reg[DMA_CONBLK_AD], 32);
	printf(")\n");
	printf("	     SOURCE_AD: 0x%x\n", dma_reg[DMA_SOURCE_AD]);
	printf("	       DEST_AD: 0x%x\n", dma_reg[DMA_DEST_AD]);
	printf("	      TXFR_LEN: 0x%x\n", dma_reg[DMA_TXFR_LEN]);
	printf("	     NEXTCONBK: 0x%x\n", dma_reg[DMA_NEXTCONBK]);
	printf("	        STRIDE: 0x%x\n", dma_reg[DMA_STRIDE]);
	printf("	            TI: 0x%x\n", dma_reg[DMA_TI]);
	printf("	            CS: 0x%x\n", dma_reg[DMA_CS]);
	printf("	         DEBUG: 0x%x\n", dma_reg[DMA_DEBUG]);
	printf("\n");
}

// Display the contents of a Control Block
void dumpControlBlock(dma_cb_t *c) {
	printf("Control Block\n");
	printf("	           TI: 0x%x\n", c->info);
	printf("	    SOURCE_AD: 0x%x\n", c->src);
	printf("	      DEST_AD: 0x%x\n", c->dst);
	printf("	     TXFR_LEN: 0x%x\n", c->length);
	printf("	       STRIDE: 0x%x\n", c->stride);
	printf("	    NEXTCONBK: 0x%x\n", c->next);
	printf("	         RES1: 0x%x\n", c->pad[0]);
	printf("	         RES2: 0x%x\n", c->pad[1]);
	printf("\n");
}

// Display the contents of a Transfer Information word
void dumpTransferInformation(unsigned int TI) {
	printf("Transfer Information (0x%x, ", TI);
	printBinary(TI, 32);
	printf(")\n");
	printf("	NO_WIDE_BURSTS: %d\n", GETBIT(TI, DMA_TI_NO_WIDE_BURSTS));
	printf("	         WAITS: %d\n", (TI >> DMA_TI_WAITS) & 0b11111);		// WAITS is bits 25:21
	printf("	        PERMAP: %d\n", (TI >> DMA_TI_PERMAP) & 0b11111);		// PERMAP is bits 20:16
	printf("	  BURST_LENGTH: %d\n", (TI >> DMA_TI_BURST_LENGTH) & 0b1111);	// BURST_LENGTH is bits 15:12
	printf("	    SRC_IGNORE: %d\n", GETBIT(TI, DMA_TI_SRC_IGNORE));
	printf("	      SRC_DREQ: %d\n", GETBIT(TI, DMA_TI_SRC_DREQ));
	printf("	     SRC_WIDTH: %d\n", GETBIT(TI, DMA_TI_SRC_WIDTH));
	printf("	       SRC_INC: %d\n", GETBIT(TI, DMA_TI_SRC_INC));
	printf("	   DEST_IGNORE: %d\n", GETBIT(TI, DMA_TI_DEST_IGNORE));
	printf("	     DEST_DREQ: %d\n", GETBIT(TI, DMA_TI_DEST_DREQ));
	printf("	    DEST_WIDTH: %d\n", GETBIT(TI, DMA_TI_DEST_WIDTH));
	printf("	      DEST_INC: %d\n", GETBIT(TI, DMA_TI_DEST_INC));
	printf("	     WAIT_RESP: %d\n", GETBIT(TI, DMA_TI_WAIT_RESP));
	printf("	        TDMODE: %d\n", GETBIT(TI, DMA_TI_TDMODE));
	printf("	         INTEN: %d\n", GETBIT(TI, DMA_TI_INTEN));
	printf("\n");
}

// Display the readable DMA registers
void dumpDMA() {

	dumpDMARegs();

	printf("DMA Control & Status Register: ");
	printBinary(dma_reg[DMA_CS], 32);
	printf("\n");
	printf("	         RESET: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_RESET));
	printf("	         ABORT: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_ABORT));
	printf("	      DISDEBUG: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_DISDEBUG));
	printf("	     PANIC_PRI: %d\n", (dma_reg[DMA_CS] >> DMA_CS_PANIC_PRI) & 0b1111);
	printf("	      PRIORITY: %d\n", (dma_reg[DMA_CS] >> DMA_CS_PRIORITY) & 0b1111);
	printf("	         ERROR: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_ERROR));
	printf("	   WAITING_FOR: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_WAITING_FOR));
	printf("	DREQ_STOPS_DMA: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_DREQ_STOPS_DMA));
	printf("	        PAUSED: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_PAUSED));
	printf("	          DREQ: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_DREQ));
	printf("	           INT: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_INT));
	printf("	           END: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_END));
	printf("	        ACTIVE: %d\n", GETBIT(dma_reg[DMA_CS], DMA_CS_ACTIVE));
	printf("\n");

	dumpTransferInformation(dma_reg[DMA_TI]);

	printf("DMA Debug Register: ");
	printBinary(dma_reg[DMA_DEBUG], 32);
	printf("\n");
	printf("	          LITE: %d\n", GETBIT(dma_reg[DMA_DEBUG], DMA_DEBUG_LITE));
	printf("	       VERSION: %d\n", (dma_reg[DMA_DEBUG] >> DMA_DEBUG_VERSION) & 0b1111);
	printf("	     DMA_STATE: %d\n", (dma_reg[DMA_DEBUG] >> DMA_DEBUG_DMA_STATE) & 0b111111111);
	printf("	        DMA_ID: %d\n", (dma_reg[DMA_DEBUG] >> DMA_DEBUG_DMA_ID) & 0b11111111);
	printf("	 OUTSTANDING W: %d\n", (dma_reg[DMA_DEBUG] >> DMA_DEBUG_OUTSTANDING_WRITES) & 0b1111);
	printf("	    READ_ERROR: %d\n", GETBIT(dma_reg[DMA_DEBUG], DMA_DEBUG_READ_ERROR));
	printf("	    FIFO_ERROR: %d\n", GETBIT(dma_reg[DMA_DEBUG], DMA_DEBUG_FIFO_ERROR));
	printf("	  READ_LAST_NS: %d\n", GETBIT(dma_reg[DMA_DEBUG], DMA_DEBUG_READ_LAST_NOT_SET));
	printf("\n");

}



// =================================================================================================
//	.___       .__  __      ___ ___                  .___                              
//	|   | ____ |__|/  |_   /   |   \_____ _______  __| _/_  _  _______ _______   ____  
//	|   |/    \|  \   __\ /    ~    \__  \\_  __ \/ __ |\ \/ \/ /\__  \\_  __ \_/ __ \ 
//	|   |   |  \  ||  |   \    Y    // __ \|  | \/ /_/ | \     /  / __ \|  | \/\  ___/ 
//	|___|___|  /__||__|    \___|_  /(____  /__|  \____ |  \/\_/  (____  /__|    \___  >
//	         \/                  \/      \/           \/              \/            \/ 
// =================================================================================================

void initHardware() {

	int i = 0;
	int pid;
	int fd;
	char pagemap_fn[64];

	// Clear the PWM buffer
	// ---------------------------------------------------------------
	clearPWMBuffer();

	// Set up peripheral access
	// ---------------------------------------------------------------
	dma_reg = map_peripheral(DMA_BASE, DMA_LEN);
	dma_reg += 0x000;
	pwm_reg = map_peripheral(PWM_BASE, PWM_LEN);
	clk_reg = map_peripheral(CLK_BASE, CLK_LEN);
	gpio_reg = map_peripheral(GPIO_BASE, GPIO_LEN);


	// Set PWM alternate function for GPIO18
	// ---------------------------------------------------------------
	//gpio_reg[1] &= ~(7 << 24);
	//usleep(100);
	//gpio_reg[1] |= (2 << 24);
	//usleep(100);
	SET_GPIO_ALT(18, 5);
	

	// Allocate memory for the DMA control block & data to be sent
	// ---------------------------------------------------------------
	virtbase = mmap(
		NULL,													// Address
		NUM_PAGES * PAGE_SIZE,									// Length
		PROT_READ | PROT_WRITE,									// Protection
		MAP_SHARED |											// Shared
		MAP_ANONYMOUS |											// Not file-based, init contents to 0
		MAP_NORESERVE |											// Don't reserve swap space
		MAP_LOCKED,												// Lock in RAM (don't swap)
		-1,														// File descriptor
		0);														// Offset

	if (virtbase == MAP_FAILED) {
		fatal("Failed to mmap physical pages: %m\n");
	}

	if ((unsigned long)virtbase & (PAGE_SIZE-1)) {
		fatal("Virtual address is not page aligned\n");
	}

	//printf("virtbase mapped 0x%x bytes at 0x%x\n", NUM_PAGES * PAGE_SIZE, virtbase);

	// Allocate page map (pointers to the control block(s) and data for each CB
	page_map = malloc(NUM_PAGES * sizeof(*page_map));
	if (page_map == 0) {
		fatal("Failed to malloc page_map: %m\n");
	} else {
		//printf("Allocated 0x%x bytes for page_map at 0x%x\n", NUM_PAGES * sizeof(*page_map), page_map);
	}

	// Use /proc/self/pagemap to figure out the mapping between virtual and physical addresses
	pid = getpid();
	sprintf(pagemap_fn, "/proc/%d/pagemap", pid);
	fd = open(pagemap_fn, O_RDONLY);

	if (fd < 0) {
		fatal("Failed to open %s: %m\n", pagemap_fn);
	}

	if (lseek(fd, (unsigned long)virtbase >> 9, SEEK_SET) != (unsigned long)virtbase >> 9) {
		fatal("Failed to seek on %s: %m\n", pagemap_fn);
	}

	//printf("Page map: %d pages\n", NUM_PAGES);
	for (i = 0; i < NUM_PAGES; i++) {
		uint64_t pfn;
		page_map[i].virtaddr = virtbase + i * PAGE_SIZE;

		// Following line forces page to be allocated
		// (Note: Copied directly from Hirst's code... page_map[i].virtaddr[0] was just set...?)
		page_map[i].virtaddr[0] = 0;

		if (read(fd, &pfn, sizeof(pfn)) != sizeof(pfn)) {
			fatal("Failed to read %s: %m\n", pagemap_fn);
		}

		if ((pfn >> 55)&0xfbf != 0x10c) {  // pagemap bits: https://www.kernel.org/doc/Documentation/vm/pagemap.txt
			fatal("Page %d not present (pfn 0x%016llx)\n", i, pfn);
		}

		page_map[i].physaddr = (unsigned int)pfn << PAGE_SHIFT | 0x40000000;
		//printf("Page map #%2d: virtual %8p ==> physical 0x%08x [0x%016llx]\n", i, page_map[i].virtaddr, page_map[i].physaddr, pfn);
	}


	// Set up control block
	// ---------------------------------------------------------------
	ctl = (struct control_data_s *)virtbase;
	dma_cb_t *cbp = ctl->cb;
	// FIXME: Change this to use DEFINEs
	unsigned int phys_pwm_fifo_addr = 0x7e20c000 + 0x18;

	// No wide bursts, source increment, dest DREQ on line 5, wait for response, enable interrupt
	cbp->info = DMA_TI_CONFIGWORD;

	// Source is our allocated memory
	cbp->src = mem_virt_to_phys(ctl->sample);
	
	// Destination is the PWM controller
	cbp->dst = phys_pwm_fifo_addr;

	// 72 bits per pixel / 32 bits per word = 2.25 words per pixel
	// Add 1 to make sure the PWM FIFO gets the message: "we're sending zeroes"
	// Times 4 because DMA works in bytes, not words
	cbp->length = ((numLEDs * 2.25) + 1) * 4;
	if(cbp->length > NUM_DATA_WORDS * 4) {
		cbp->length = NUM_DATA_WORDS * 4;
	}

	// We don't use striding
	cbp->stride = 0;
	
	// These are reserved
	cbp->pad[0] = 0;
	cbp->pad[1] = 0;
	
	// Pointer to next block - 0 shuts down the DMA channel when transfer is complete
	cbp->next = 0;

	// Testing
	/*
	ctl = (struct control_data_s *)virtbase;
	ctl->sample[0] = 0x00000000;
	ctl->sample[1] = 0x000000FA;
	ctl->sample[2] = 0x0000FFFF;
	ctl->sample[3] = 0xAAAAAAAA;
	ctl->sample[4] = 0xF0F0F0F0;
	ctl->sample[5] = 0x0A0A0A0A;
	ctl->sample[6] = 0xF00F0000;
	*/


	// Stop any existing DMA transfers
	// ---------------------------------------------------------------
	dma_reg[DMA_CS] |= (1 << DMA_CS_ABORT);
	usleep(100);
	dma_reg[DMA_CS] = (1 << DMA_CS_RESET);
	usleep(100);


	// PWM Clock
	// ---------------------------------------------------------------
	// Kill the clock
	// FIXME: Change this to use a DEFINE
	clk_reg[PWM_CLK_CNTL] = 0x5A000000 | (1 << 5);
	usleep(100);

	// Disable DMA requests
	CLRBIT(pwm_reg[PWM_DMAC], PWM_DMAC_ENAB);
	usleep(100);

	// The fractional part is quantized to a range of 0-1024, so multiply the decimal part by 1024.
	// E.g., 0.25 * 1024 = 256.
	// So, if you want a divisor of 400.5, set idiv to 400 and fdiv to 512.
	unsigned int idiv = 400;
	unsigned short fdiv = 0;	// Should be 16 bits, but the value must be <= 1024
	clk_reg[PWM_CLK_DIV] = 0x5A000000 | (idiv << 12) | fdiv;	// Set clock multiplier
	usleep(100);

	// Enable the clock. Next-to-last digit means "enable clock". Last digit is 1 (oscillator),
	// 4 (PLLA), 5 (PLLC), or 6 (PLLD) (according to the docs) although PLLA doesn't seem to work.
	// FIXME: Change this to use a DEFINE
	clk_reg[PWM_CLK_CNTL] = 0x5A000015;
	usleep(100);


	// PWM
	// ---------------------------------------------------------------
	// Clear any preexisting crap from the control & status register
	pwm_reg[PWM_CTL] = 0;

	// Set transmission range (32 bytes, or 1 word)
	// <32: Truncate. >32: Pad with SBIT1. As it happens, 32 is perfect.
	pwm_reg[PWM_RNG1] = 32;
	usleep(100);
	
	// Send DMA requests to fill the FIFO
	pwm_reg[PWM_DMAC] =
		(1 << PWM_DMAC_ENAB) |
		(8 << PWM_DMAC_PANIC) |
		(8 << PWM_DMAC_DREQ);
	usleep(1000);
	
	// Clear the FIFO
	SETBIT(pwm_reg[PWM_CTL], PWM_CTL_CLRF1);
	usleep(100);
	
	// Don't repeat last FIFO contents if it runs dry
	CLRBIT(pwm_reg[PWM_CTL], PWM_CTL_RPTL1);
	usleep(100);
	
	// Silence (default) bit is 0
	CLRBIT(pwm_reg[PWM_CTL], PWM_CTL_SBIT1);
	usleep(100);
	
	// Polarity = default (low = 0, high = 1)
	CLRBIT(pwm_reg[PWM_CTL], PWM_CTL_POLA1);
	usleep(100);
	
	// Enable serializer mode
	SETBIT(pwm_reg[PWM_CTL], PWM_CTL_MODE1);
	usleep(100);
	
	// Use FIFO rather than DAT1
	SETBIT(pwm_reg[PWM_CTL], PWM_CTL_USEF1);
	usleep(100);

	// Disable MSEN1
	CLRBIT(pwm_reg[PWM_CTL], PWM_CTL_MSEN1);
	usleep(100);
	

	// DMA
	// ---------------------------------------------------------------
	// Raise an interrupt when transfer is complete, which will set the INT flag in the CS register
	SETBIT(dma_reg[DMA_CS], DMA_CS_INT);
	usleep(100);
	
	// Clear the END flag (by setting it - this is a "write 1 to clear", or W1C, bit)
	SETBIT(dma_reg[DMA_CS], DMA_CS_END);
	usleep(100);
	
	// Send the physical address of the control block into the DMA controller
	dma_reg[DMA_CONBLK_AD] = mem_virt_to_phys(ctl->cb);
	usleep(100);
	
	// Clear error flags, if any (these are also W1C bits)
	// FIXME: Use a define instead of this
	dma_reg[DMA_DEBUG] = 7;
	usleep(100);
}

// Begin the transfer
void startTransfer() {
	// Enable DMA
	dma_reg[DMA_CONBLK_AD] = mem_virt_to_phys(ctl->cb);
	dma_reg[DMA_CS] = DMA_CS_CONFIGWORD | (1 << DMA_CS_ACTIVE);
	usleep(100);

	// Enable PWM
	SETBIT(pwm_reg[PWM_CTL], PWM_CTL_PWEN1);

//	dumpPWM();
//	dumpDMA();
}



// =================================================================================================
//	  ____ ___            .___       __           .____     ___________________          
//	 |    |   \______   __| _/____ _/  |_  ____   |    |    \_   _____/\______ \   ______
//	 |    |   /\____ \ / __ |\__  \\   __\/ __ \  |    |     |    __)_  |    |  \ /  ___/
//	 |    |  / |  |_> > /_/ | / __ \|  | \  ___/  |    |___  |        \ |    `   \\___ \ 
//	 |______/  |   __/\____ |(____  /__|  \___  > |_______ \/_______  //_______  /____  >
//	           |__|        \/     \/          \/          \/        \/         \/     \/ 
// =================================================================================================

void show() {

	// Clear out the PWM buffer
	// Disabled, because we will overwrite the buffer anyway.

	// Read data from LEDBuffer[], translate it into wire format, and write to PWMWaveform
	int i, j;
	unsigned int LEDBuffeWordPos = 0;
	unsigned int PWMWaveformBitPos = 0;
	unsigned int colorBits = 0;			// Holds the GRB color before conversion to wire bit pattern
	unsigned char colorBit = 0;			// Holds current bit out of colorBits to be processed
	unsigned int wireBit = 0;			// Holds the current bit we will set in PWMWaveform
	Color_t color;

	for(i=0; i<numLEDs; i++) {
		// Create bits necessary to represent one color triplet (in GRB, not RGB, order)
		//printf("RGB: %d, %d, %d\n", LEDBuffer[i].r, LEDBuffer[i].g, LEDBuffer[i].b);
		LEDBuffer[i].r *= brightness;
		LEDBuffer[i].g *= brightness;
		LEDBuffer[i].b *= brightness;
		colorBits = ((unsigned int)LEDBuffer[i].r << 8) | ((unsigned int)LEDBuffer[i].g << 16) | LEDBuffer[i].b;
		//printBinary(colorBits, 24);
		//printf(" (binary, GRB order)\n");

		// Iterate through color bits to get wire bits
		for(j=23; j>=0; j--) {
			colorBit = (colorBits & (1 << j)) ? 1 : 0;
			switch(colorBit) {
				case 1:
					//wireBits = 0b110;	// High, High, Low
					setPWMBit(wireBit++, 1);
					setPWMBit(wireBit++, 1);
					setPWMBit(wireBit++, 0);
					break;
				case 0:
					//wireBits = 0b100;	// High, Low, Low
					setPWMBit(wireBit++, 1);
					setPWMBit(wireBit++, 0);
					setPWMBit(wireBit++, 0);
					break;
			}
		}
	}

	// Copy PWM waveform to DMA's data buffer
	//printf("Copying %d words to DMA data buffer\n", NUM_DATA_WORDS);
	ctl = (struct control_data_s *)virtbase;
	dma_cb_t *cbp = ctl->cb;

	// This block is a major CPU hog when there are lots of pixels to be transmitted.
	// It would go quicker with DMA.
	for(i = 0; i < (cbp->length / 4); i++) {
		ctl->sample[i] = PWMWaveform[i];
	}


	// Enable DMA and PWM engines, which should now send the data
	startTransfer();

	// Wait long enough for the DMA transfer to finish
	// 3 RAM bits per wire bit, so 72 bits to send one color command.
	float bitTimeUSec = (float)(NUM_DATA_WORDS * 32) * 0.4;	// Bits sent * time to transmit one bit, which is 0.4μSec
	//printf("Delay for %d μSec\n", (int)bitTimeUSec);
	usleep((int)bitTimeUSec);
/*

This is the old FIFO-filling code.
The FIFO only has enough words for about 7 LEDs, which is why we use DMA instead!

	for(i=0; i<NUM_DATA_WORDS; i++) {
		
		// That done, we add the word to the FIFO
		printf("Adding word to FIFO: ");
		printBinary(PWMWaveform[i], 32);
		printf("\n");
		pwm_reg[PWM_FIF1] = PWMWaveform[i];
//		*(pwm + PWM_FIF1) = 0xACAC00F0;	// A test pattern easily visible on an oscilloscope, but not WS2812 compatible
		usleep(20);
	}
/**/

}



// =================================================================================================
//	___________ _____  _____              __          
//	\_   _____// ____\/ ____\____   _____/  |_  ______
//	 |    __)_\   __\\   __\/ __ \_/ ___\   __\/  ___/
//	 |        \|  |   |  | \  ___/\  \___|  |  \___ \ 
//	/_______  /|__|   |__|  \___  >\___  >__| /____  >
//	        \/                  \/     \/          \/ 
// =================================================================================================
// The effects in this section are adapted from the Adafruit NeoPixel library at:
// https://github.com/adafruit/Adafruit_NeoPixel/blob/master/examples/strandtest/strandtest.ino

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
Color_t Wheel(uint8_t WheelPos) {
	if(WheelPos < 85) {
		return Color(WheelPos * 3, 255 - WheelPos * 3, 0);
	} else if(WheelPos < 170) {
		WheelPos -= 85;
		return Color(255 - WheelPos * 3, 0, WheelPos * 3);
	} else {
		WheelPos -= 170;
		return Color(0, WheelPos * 3, 255 - WheelPos * 3);
	}
}


// Fill the dots one after the other with a color
void colorWipe(Color_t c, uint8_t wait) {
	uint16_t i;
	for(i=0; i<numPixels(); i++) {
		setPixelColorT(i, c);
		show();
		usleep(wait * 1000);
	}
}

// Rainbow
void rainbow(uint8_t wait) {
	uint16_t i, j;

	for(j=0; j<256; j++) {
		for(i=0; i<numPixels(); i++) {
			setPixelColorT(i, Wheel((i+j) & 255));
		}
		show();
		usleep(wait * 1000);
	}
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
	uint16_t i, j;

	for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
		for(i=0; i<numPixels(); i++) {
			setPixelColorT(i, Wheel(((i * 256 / numPixels()) + j) & 255));
		}
		show();
		usleep(wait * 1000);
	}
}

//Theatre-style crawling lights.
void theaterChase(Color_t c, uint8_t wait) {
	unsigned int j, q, i;
	for (j=0; j<15; j++) {  //do this many cycles of chasing
		for (q=0; q < 3; q++) {
			for (i=0; i < numPixels(); i=i+3) {
				setPixelColorT(i+q, c);			// Turn every third pixel on
			}
			show();
     
			usleep(wait * 1000);

			for (i=0; i < numPixels(); i=i+3) {
				setPixelColor(i+q, 0, 0, 0);	// Turn every third pixel off
			}
		}
	}
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
	int j, q, i;
	for (j=0; j < 256; j+=4) {     // cycle through every 4th color on the wheel
		for (q=0; q < 3; q++) {
			for (i=0; i < numPixels(); i=i+3) {
				setPixelColorT(i+q, Wheel((i+j) % 255));    //turn every third pixel on
			}
			show();

			usleep(wait * 1000);
       
			for (i=0; i < numPixels(); i=i+3) {
				setPixelColor(i+q, 0, 0, 0);        //turn every third pixel off
			}
		}
	}
}



// =================================================================================================
//	   _____         .__        
//	  /     \ _____  |__| ____  
//	 /  \ /  \\__  \ |  |/    \ 
//	/    Y    \/ __ \|  |   |  \
//	\____|__  (____  /__|___|  /
//	        \/     \/        \/ 
// =================================================================================================

void effectsDemo() {

	int i, j, ptr;
	float k;

	// Default effects from the Arduino lib
	colorWipe(Color(255, 0, 0), 50); // Red
	colorWipe(Color(0, 255, 0), 50); // Green
	colorWipe(Color(0, 0, 255), 50); // Blue
	theaterChase(Color(127, 127, 127), 50); // White
	theaterChase(Color(127,   0,   0), 50); // Red
	theaterChase(Color(  0,   0, 127), 50); // Blue
	rainbow(5);
	rainbowCycle(5);
	theaterChaseRainbow(50);

	// Watermelon fade :)
	for(k=0; k<0.5; k+=.01) {
		ptr=0;
		setBrightness(k);
		for(i=0; i<numLEDs; i++) {
			setPixelColor(i, i*5, 64, i*2);
		}
		show();
	}
	for(k=0.5; k>=0; k-=.01) {
		ptr=0;
		setBrightness(k);
		for(i=0; i<numLEDs; i++) {
			setPixelColor(i, i*5, 64, i*2);
		}
		show();
	}
	usleep(1000);

	// Random color fade
	srand(time(NULL));
	uint8_t lastRed = 0;
	uint8_t lastGreen = 0;
	uint8_t lastBlue = 0;
	uint8_t red, green, blue;
	Color_t curPixel;
	setBrightness(DEFAULT_BRIGHTNESS);
	for(j=1; j<16; j++) {
		ptr = 0;
		if(j % 3) {
			red = 120;
			green = 64;
			blue = 48;
		} else if(j % 7) {
			red = 255;
			green = 255;
			blue = 255;
		} else {
			red = rand();
			green = rand();
			blue = rand();
		}
		for(k=0; k<1; k+=.01) {
			for(i=0; i<numLEDs; i++) {
				setPixelColor(
					i,
					(red * k) + (lastRed * (1-k)),
					i * (255 / numLEDs), //(green * k) + (lastGreen * (1-k)),
					(blue * k) + (lastBlue * (1-k))
					);
				curPixel = getPixelColor(i);
			}
			show();
		}
		lastRed = red;
		lastGreen = green;
		lastBlue = blue;
	}
}