#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define verbose 0
//#define repeat_times 1000  //For timing loop
#define repeat_times 10

const TTAP_ctl_addr = 0x000;
const TTAP_descr_mem_base = 0x400;
const TTAP_descr_pointer_mask = 0x3ff;
const TTAP_btel_mem_base = 0x100;
const TTAP_btel_pointer_mask = 0xff;
const TTAP_descr_pointer_lsbs_addr = 0x001;
const TTAP_descr_pointer_msbs_addr = 0x002;
const TTAP_btel_pointer_addr = 0x003;
const TTAP_dflt_descr_duration_lsbs_addr = 0x004;
const TTAP_dflt_descr_duration_midbs_addr = 0x005;
const TTAP_dflt_descr_duration_msbs_addr = 0x006;
const TTAP_init = 0x00;
const TTAP_ena_stop_on_done = 0x06;
const TTAP_ena_stop_on_done_run = 0x07;
const TTAP_intr_ena_ena_stop_on_done_run = 0x0f;
const TTAP_descr_header = 0x20; // spi_ena only
const TTAP_descr_header_w_intr = 0xa0; // intr_ena and spi_ena
const TTAP_descr_w_delay_header = 0x23; // spi_ena, 24-bit duration field
const TTAP_descr_w_delay_intr_header = 0xa3; // spi_ena, 24-bit duration field
const TTAP_ftel_header = 0x00; // Default; no FTEL for now

//#define default_descr_duration 2000
#define default_descr_duration 0
//#define descr_descr_duration 70000 // Just over 16 bits; this works
#define descr_descr_duration 0

int descr_pointer_local, btel_pointer_local;

void spi_write(volatile unsigned long *mem_base, int descr_pointer, int addr, int byte_count, char write_data[]); 
void spi_read(volatile unsigned long *mem_base, int descr_pointer, int btel_pointer, int addr, int byte_count, char *read_data);
void wait_for_interrupt (volatile unsigned long *mem_base);

void mydelay (int maxcnt);

int main(int argc, char *argv[])
{

	const off_t mem_base_address = 0x30000000;  

	int alloc_mem_size, page_mask, page_size;

	const int byte_mult = 4;
	const int mem_size = 0x800 * byte_mult;

	page_size = sysconf(_SC_PAGESIZE);
	alloc_mem_size = (((mem_size / page_size) + 1) * page_size);
	page_mask = (page_size - 1);

	char *mem_pointer;
	volatile unsigned long * word_pointer;

	int TTAP_descr_pointer;
	int TTAP_btel_pointer;

	int addr, data, byte_count;
	int i, j;
	int ctl;
	char data_buf[128];

	int mem_dev = open("/dev/mem", O_RDWR | O_SYNC);

	if(mem_dev == -1)
	{
   // Error
	}


	mem_pointer = mmap(NULL,
					   alloc_mem_size,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED,
					   mem_dev,
					   (mem_base_address & ~page_mask)
					  );

	if(mem_pointer == MAP_FAILED)
	{  
		printf("Error -- mmap failed");
		exit(0);
	}

	word_pointer = (unsigned long *) mem_pointer;

	// Don't wait for RUN to clear  -- assume that previous calls already waited for "interrupt"
	//while (1 & *(word_pointer + TTAP_ctl_addr));

	*(word_pointer + TTAP_ctl_addr) = TTAP_ena_stop_on_done;  // Set ENA if not already set


	//-------------------------------------------------------------------------------
	int exportfd, directionfd;
	//Open GPIO[3] for polling "interrupt" line
	exportfd = open("/sys/class/gpio/export", O_WRONLY);
	if (exportfd < 0)
	{
		printf("Cannot open GPIO to export it\n");
		exit(1);
	}
	write(exportfd, "3", 4); //"Interrupt" input from GPIO[3]
	close(exportfd);
	if(verbose) printf("GPIO[3] exported successfully\n");

	// Update the direction of the GPIO 3 to be an input
	directionfd = open("/sys/class/gpio/gpio3/direction", O_RDWR);
	if (directionfd < 0)
	{
		printf("Cannot open GPIO[3] direction in\n");
		exit(1);
	}
	write(directionfd, "in", 4);
	close(directionfd);
	if (verbose) printf("GPIO 3 direction set as input successfully\n");
//---------------------------------------------------------------------------------
	
//Read the descriptor RAM pointer. In a case where reads/writes were being called multiple times within same program, the pointer value
//coud be mainained in the C program
	TTAP_descr_pointer = 0xff & (*(word_pointer + TTAP_descr_pointer_lsbs_addr));
	TTAP_descr_pointer = TTAP_descr_pointer | ((0x03 & (*(word_pointer + TTAP_descr_pointer_msbs_addr))) << 8);

	if(verbose) printf("TTAP_descr_pointer = 0x%x\n", TTAP_descr_pointer);

//Set the default descriptor duration -- really just for debugging the duration feature right now
	*(word_pointer + TTAP_dflt_descr_duration_lsbs_addr) = 0xff & default_descr_duration;
	*(word_pointer + TTAP_dflt_descr_duration_midbs_addr) = 0xff & (default_descr_duration >> 8);
	*(word_pointer + TTAP_dflt_descr_duration_msbs_addr) = 0xff & (default_descr_duration >> 16);
	
// Now read the args and do the SPI transfer
	if((argv[1][0] == 'w') || (argv[1][0] == 'W')) { //SPI write
		sscanf(argv[2], "%x", &addr);
		if (verbose) printf("Write Addr = 0%d\n",addr);
		byte_count = argc - 3;
		for(j = 3; j < argc; j++) { // get the data bytes
			sscanf(argv[j], "%x", &data);
			data_buf[j-3] = data;
		}

		for(j = 0; j < repeat_times; j++){
		spi_write(word_pointer, TTAP_descr_pointer, addr, byte_count, data_buf);
		TTAP_descr_pointer = descr_pointer_local;
	//	while ((repeat_times > 1) && (1 & *(word_pointer + TTAP_ctl_addr))); // Wait for RUN to clear  -- no longer necessary


		}
	}

	else if ((argv[1][0] == 'r') || (argv[1][0] == 'R')) { //SPI read
		sscanf(argv[2], "%x", &addr);
		sscanf(argv[3], "%x", &byte_count);
		if (verbose) printf("Read Addr = %x, byte_count = %x\n",addr,byte_count);

	//Read the BTEL RAM pointer. In a case where reads/writes were being called multiple times within same program, the pointer value
	//coud be mainained in the C program
		TTAP_btel_pointer = 0xff & *(word_pointer + TTAP_btel_pointer_addr);

		if(verbose) printf("TTAP_btel_pointer = 0x%x\n", TTAP_btel_pointer);

		for(j = 0; j < repeat_times; j++){
		spi_read(word_pointer, TTAP_descr_pointer, TTAP_btel_pointer, addr, byte_count, &data_buf[0]);
		TTAP_descr_pointer = descr_pointer_local;
		TTAP_btel_pointer = btel_pointer_local;
		}


		for(j = 0; j < byte_count; j++)  // print data bytes
			printf("%x ",data_buf[j]);
		printf("\n");
	}

	else if ((argv[1][0] == 'p') || (argv[1][0] == 'P')) { //Power reset
		printf("Power reset not supported (yet)\n");
	}


	else printf("Unsupported command\n");


	munmap(mem_pointer, alloc_mem_size);
	close(mem_dev);

}

//Assumes ENA is already set, RUN is off
void spi_write(volatile unsigned long *mem_base, int descr_pointer, int addr, int byte_count, char write_data[]) {
	int i;
	//int descr_pointer_local;
	int next_descr_pointer;

	descr_pointer_local = descr_pointer;
	
	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = TTAP_descr_header_w_intr;  //Write descr header
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = TTAP_ftel_header;  //Write ftel header
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

//	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = byte_count - 1;  //Write byte count
	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = byte_count;      //Write byte count  NEW: byte count includes inst/address
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = 0x80 | addr;  //Write inst/addr
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	if(verbose) printf("byte_count = 0x%x\n",byte_count);
	
	for(i = 0; i < byte_count; i++) {  //Write data bytes
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = write_data[i];  
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
	}

	//Write next desriptor address
	next_descr_pointer = (descr_pointer_local + 2) & TTAP_descr_pointer_mask;
	*(mem_base + TTAP_descr_mem_base +descr_pointer_local) = next_descr_pointer & 0xff;
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
	*(mem_base + TTAP_descr_mem_base +descr_pointer_local) = (next_descr_pointer >> 8) & 0x3;
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	if(verbose) {
		descr_pointer_local = descr_pointer;
		for(i = 0; i < byte_count+6; i++) {
			printf("Descr_ram @ 0x%x = 0x%x\n", descr_pointer_local, *(mem_base + TTAP_descr_mem_base + descr_pointer_local));
			descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
		}

	}
			
	*(mem_base + TTAP_ctl_addr) = TTAP_intr_ena_ena_stop_on_done_run;  //Set run

	if(verbose) printf("TTAP ctl reg = 0x%2x\n", *(mem_base + TTAP_ctl_addr));
	
	//Wait for write to complete before returning
	wait_for_interrupt(mem_base);

}

void spi_read(volatile unsigned long *mem_base, int descr_pointer, int btel_pointer, int addr, int byte_count, char *read_data) {
	int i, data, ctl;
	//int descr_pointer_local, btel_pointer_local;
	int next_descr_pointer;

	descr_pointer_local = descr_pointer;
	btel_pointer_local = btel_pointer;

	if(verbose) printf("byte_count = 0x%x\n",byte_count);

	if(descr_descr_duration == 0) {
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = TTAP_descr_header_w_intr;  //Write descr header
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
	}
	else {
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = TTAP_descr_w_delay_intr_header;  //Write descr header w/ 3-byte duration  in descriptor
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = 0xff & descr_descr_duration;  //Write descr duration lsbs
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = 0xff & (descr_descr_duration >> 8);  //Write descr duration middle bits
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
		*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = 0xff & (descr_descr_duration >> 16);  //Write descr duration msbs
		descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
	}


	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = TTAP_ftel_header;  //Write ftel header
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

//	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = byte_count - 1;  //Write byte count
	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = byte_count;      //Write byte count  NEW: byte count includes inst/address
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	*(mem_base + TTAP_descr_mem_base + descr_pointer_local) = 0x7f & addr;  //Write inst/addr
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	//Write next desriptor address
	next_descr_pointer = (descr_pointer_local + 2) & TTAP_descr_pointer_mask;
	*(mem_base + TTAP_descr_mem_base +descr_pointer_local) = next_descr_pointer & 0xff;
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
	*(mem_base + TTAP_descr_mem_base +descr_pointer_local) = (next_descr_pointer >> 8) & 0x3;
	descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;

	if(verbose) {
		descr_pointer_local = descr_pointer;
		for(i = 0; i < 6; i++) {
			printf("Descr_ram @ 0x%x = 0x%x\n", descr_pointer_local, *(mem_base + TTAP_descr_mem_base + descr_pointer_local));
			descr_pointer_local = (descr_pointer_local + 1) & TTAP_descr_pointer_mask ;
		}
	}
	
	*(mem_base + TTAP_ctl_addr) = TTAP_intr_ena_ena_stop_on_done_run;  //Set run

	// Wait for RUN to clear
	//while (1 & *(mem_base + TTAP_ctl_addr));

	wait_for_interrupt(mem_base);

	//mydelay(20);
	
	for(i = 0; i < byte_count; i++) {  //read data bytes
			data = *(mem_base + TTAP_btel_mem_base + btel_pointer_local);
			if(verbose) printf("BTEL data @ buffer addr 0x%x =0x%x\n", btel_pointer_local, data);
			*(read_data + i) = data;
			btel_pointer_local = (btel_pointer_local + 1) & TTAP_btel_pointer_mask ;
		}
}

void wait_for_interrupt (volatile unsigned long *mem_base)
{
	int valuefd3;
	char gpio_val;
	int interrupt;
	int errno;
	int ctl;


	interrupt = 0;
	
	while(!interrupt) {
		valuefd3 = open("/sys/class/gpio/gpio3/value", O_RDONLY); 
		if (valuefd3 < 0)
		{
			printf("Cannot open GPIO[3] value\n");
			exit(1);
		}

		errno = read(valuefd3, &gpio_val, 1);
		if (gpio_val == '1') {
			interrupt = 1;
			if(verbose) printf("GPIO 0 = 1\n");
		}
		else{
			interrupt = 0;
			if (verbose) printf("GPIO 0 = 0\n");
		}
	close(valuefd3); 
	}

	//Clear interrupt by writing value of intr_flag into that bit
	*(mem_base + TTAP_ctl_addr) = *(mem_base + TTAP_ctl_addr);  
}

	

	//-------------------------------------------------------------------------------

void mydelay (int maxcnt)
{
	int val, i, j, k;
	for (i=0; i<maxcnt; i++)
		for (j=0; j<maxcnt; j++)
			for(k=0;k<maxcnt; k++)
				val += 1;
}