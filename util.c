#include "util.h"

// probably have to tweak these if the processor speed changes. Just have to be rough
void delay_us(const uint16_t us)
{
   uint32_t i = us * 60;
   while (i-- > 0) {
      __asm volatile ("nop");
   }
}

void delay_ms(const uint16_t ms)
{
   //uint32_t i = ms * 27778;
   uint32_t i = ms * 30000 *2;
   while (i-- > 0) {
      __asm volatile ("nop");
   }
}

void blink_debug_led(int delay) {
        while(1) {
                GPIOA->ODR |= GPIO_DEBUG_LED;
                delay_ms(delay);
                GPIOA->ODR &= ~(GPIO_DEBUG_LED);
                delay_ms(delay);
        }
}

uint32_t suffix_match(char *name, char *suffix) {
	if (strlen(name)>strlen(suffix)) {
		if (strncasecmp (&name[strlen(name)-strlen(suffix)],suffix,strlen(suffix)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

// Load fname rom file. 8K files get repeated up to 16K
FRESULT load_rom(char *fname, char* highbuffer) {
	FRESULT res;
        FIL     fil;
        UINT BytesRead;
	uint32_t *p;
	uint32_t *q;

	memset(&fil, 0, sizeof(FIL));

	res =  f_open(&fil, fname, FA_READ);

	if (res == FR_OK) {
		res = f_read(&fil,highbuffer, 16384, &BytesRead);
		if (res != FR_OK) {
			blink_debug_led(3000);
		} else {
			if (BytesRead == 8192) {
				p = (uint32_t *) highbuffer;
   				q = (uint32_t *) &highbuffer[8192];
   				for (int i=0; i< 0x0800;i++) {
      					*q++ = *p++;
   				}
			}
#ifdef ENABLE_SEMIHOSTING
	                printf("Read %d bytes in to %0x08x\n",BytesRead,highbuffer);
#endif
		}
		
	}
	f_close(&fil);
	return res;
}

uint32_t is_disk_image(char *name) {
	return suffix_match(name, DSK_SUFFIX) || suffix_match(name, VDK_SUFFIX);
}

// Is this something the emulator can present? Directories, hidden/system files and anything
// starting with '.' (eg. the ._xxx files macOS leaves behind) are skipped.
uint32_t is_image_file(FILINFO *fno) {
	if (fno->fattrib & (AM_DIR | AM_HID | AM_SYS)) {
		return FALSE;
	}
	if (fno->fname[0] == '.') {
		return FALSE;
	}
	return is_disk_image(fno->fname) || suffix_match(fno->fname, ROM_SUFFIX) ||
	       suffix_match(fno->fname, CCC_SUFFIX) || suffix_match(fno->fname, BIN_SUFFIX);
}

// A folder the menu can open
uint32_t is_folder(FILINFO *fno) {
	return (fno->fattrib & AM_DIR) && !(fno->fattrib & (AM_HID | AM_SYS)) && fno->fname[0] != '.';
}

// A cassette image. Only the menu can start these, so NEXT/PREV (readdir_image) skip them
uint32_t is_cas_file(FILINFO *fno) {
	return !(fno->fattrib & (AM_DIR | AM_HID | AM_SYS)) && fno->fname[0] != '.' &&
	       suffix_match(fno->fname, CAS_SUFFIX);
}

// f_readdir that skips everything is_image_file() does not like. fno->fname[0] == 0 at the end
FRESULT readdir_image(DIR *dir, FILINFO *fno) {
	FRESULT res;

	do {
		res = f_readdir(dir, fno);
	} while (res == FR_OK && fno->fname[0] != 0 && !is_image_file(fno));
	return res;
}

// Same, but folders are kept too. This is what the menu shows
FRESULT readdir_menu(DIR *dir, FILINFO *fno) {
	FRESULT res;

	do {
		res = f_readdir(dir, fno);
	} while (res == FR_OK && fno->fname[0] != 0 && !is_image_file(fno) && !is_folder(fno) && !is_cas_file(fno));
	return res;
}

// Position of each sorted menu entry among the readdir_menu() entries in directory order.
// This is what is needed to find the file or folder again when the menu selects an entry.
static uint16_t menu_ordinal[MENU_MAX_ENTRIES];

uint32_t menu_entry_ordinal(uint32_t index) {
	return (index < MENU_MAX_ENTRIES) ? menu_ordinal[index] : 0;
}

static int upper(int c) {
	return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

// Folders first, then case insensitive by name (skipping the type byte)
static int entry_compare(unsigned char *a, unsigned char *b) {
	int i;
	int a_rank = (a[0] == MENU_TYPE_FOLDER) ? 0 : 1;
	int b_rank = (b[0] == MENU_TYPE_FOLDER) ? 0 : 1;

	if (a_rank != b_rank) {
		return a_rank - b_rank;
	}
	for (i = 1; i < MENU_ENTRY_SIZE; i++) {
		if (upper(a[i]) != upper(b[i]) || a[i] == 0) {
			return upper(a[i]) - upper(b[i]);
		}
	}
	return 0;
}

static void entry_swap(unsigned char *buffer, uint32_t a, uint32_t b) {
	uint32_t *p = (uint32_t *) &buffer[a * MENU_ENTRY_SIZE];
	uint32_t *q = (uint32_t *) &buffer[b * MENU_ENTRY_SIZE];
	uint32_t t;
	int i;

	for (i = 0; i < MENU_ENTRY_SIZE / 4; i++) {
		t = p[i];
		p[i] = q[i];
		q[i] = t;
	}
	t = menu_ordinal[a];
	menu_ordinal[a] = menu_ordinal[b];
	menu_ordinal[b] = t;
}

// Shell sort of entries first to count-1. No recursion and no library calls (newlib is not
// built with -ffixed-r10)
static void sort_entries(unsigned char *buffer, uint32_t first, uint32_t count) {
	uint32_t gap, i, j, n = count - first;

	if (count <= first) {
		return;
	}
	for (gap = 1; gap < n / 3; gap = gap * 3 + 1);
	for (; gap > 0; gap /= 3) {
		for (i = first + gap; i < count; i++) {
			for (j = i; j >= first + gap && entry_compare(&buffer[(j - gap) * MENU_ENTRY_SIZE], &buffer[j * MENU_ENTRY_SIZE]) > 0; j -= gap) {
				entry_swap(buffer, j - gap, j);
			}
		}
	}
}

// Build the menu list from the folders and the rom and disk images in dirname. Each entry is
// MENU_ENTRY_SIZE bytes (see defines.h): a type byte, then the name without the extension.
// With with_up there is a ".." entry first. Then folders, then files, each sorted by name.
// Returns the number of entries.
uint32_t load_directory(char *dirname, unsigned char *buffer, uint32_t with_up) {
	FRESULT res;
	DIR dir;
	static FILINFO fno;
	uint32_t count, ordinal, len, first;
	FSIZE_t size_k;
	unsigned char *entry;
	char *dot;

	count = 0;
	if (with_up) {
		memset(buffer, 0, MENU_ENTRY_SIZE);
		buffer[0] = MENU_TYPE_UP;
		strcpy((char *) &buffer[1], "..");
		menu_ordinal[0] = 0;
		count = 1;
	}
	first = count;

	memset(&dir, 0, sizeof(DIR));
	if (f_opendir(&dir, (TCHAR *) dirname) != FR_OK) {
		return count;
	}

	ordinal = 0;
	while (count < MENU_MAX_ENTRIES) {
		res = readdir_menu(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0) {
			break;
		}
		entry = &buffer[count * MENU_ENTRY_SIZE];
		size_k = 0;
		if (is_folder(&fno)) {
			entry[0] = MENU_TYPE_FOLDER;
			len = strlen(fno.fname);
		} else {
			if (is_cas_file(&fno)) {
				entry[0] = MENU_TYPE_CAS;
			} else {
				entry[0] = is_disk_image(fno.fname) ? MENU_TYPE_DISK : MENU_TYPE_ROM;
			}
			dot = strrchr(fno.fname, '.');
			len = dot ? (uint32_t) (dot - fno.fname) : strlen(fno.fname);
			// to the nearest K, but never 0K for a file with something in it
			size_k = (fno.fsize + 512) / 1024;
			if (size_k == 0 && fno.fsize > 0) {
				size_k = 1;
			}
			if (size_k > 0xFFFF) {
				size_k = 0xFFFF;
			}
		}
		if (len > MENU_NAME_LENGTH) {
			len = MENU_NAME_LENGTH;
		}
		memcpy(&entry[1], fno.fname, len);
		memset(&entry[1 + len], 0, MENU_ENTRY_SIZE_K - 1 - len);
		entry[MENU_ENTRY_SIZE_K] = size_k & 0xFF;
		entry[MENU_ENTRY_SIZE_K + 1] = size_k >> 8;
		menu_ordinal[count] = ordinal;
		count++;
		ordinal++;
	}
	f_closedir(&dir);

	sort_entries(buffer, first, count);
	return count;
}

// ---------------------------------------------------------------------------------------------
// .cas cassette images. A file on tape is a name block, data blocks and an EOF block. Each block
// is a leader of $55s, the $3C sync byte, block type, length, the data and a checksum (the sum of
// type, length and data).
#define CAS_LEADER		0x55
#define CAS_SYNC		0x3C
#define CAS_BLOCK_NAME		0x00
#define CAS_BLOCK_DATA		0x01
#define CAS_BLOCK_EOF		0xFF
#define CAS_FILETYPE_BASIC	0
#define CAS_FILETYPE_MC		2
#define CAS_FILETYPE_STATE	'S'	// ours, from tools/cas_snapshot.py: registers to start from

typedef struct {
	FIL fil;
	uint8_t buf[512];
	UINT len, pos;
} cas_reader;

// Where in the file the next cas_getc() comes from
static FSIZE_t cas_tell(cas_reader *r) {
	return f_tell(&r->fil) - (r->len - r->pos);
}

// Next byte of the file, or -1 at the end
static int cas_getc(cas_reader *r) {
	if (r->pos >= r->len) {
		r->pos = 0;
		if (f_read(&r->fil, r->buf, sizeof(r->buf), &r->len) != FR_OK || r->len == 0) {
			r->len = 0;
			return -1;
		}
	}
	return r->buf[r->pos++];
}

// Fill in the length of the file just copied. header is its 'M', 'B' or 'S'
static const char *cas_finish(uint8_t *header, uint8_t *out) {
	uint32_t header_size = (header[0] == 'M') ? 7 : 3;
	uint32_t length = out - header - header_size;

	if (length > 0xFFFF) {
		return "CAS FILE TOO BIG";
	}
	header[header_size - 2] = length >> 8;
	header[header_size - 1] = length & 0xFF;
	return 0;
}

// One pass over the .cas, adding to *outp. want_basic FALSE copies every machine code file,
// TRUE copies the first tokenised BASIC program. *ascii_basic is set if there is a BASIC program
// saved as text, which can not be loaded this way.
static const char *cas_pass(char *fname, uint32_t want_basic, uint8_t **outp, uint8_t *end, uint32_t *ascii_basic) {
	static cas_reader r;
	static uint8_t block[255];
	uint8_t *out = *outp;
	uint8_t *header = 0;		// the file being copied, 0 while skipping
	const char *err = 0;
	int c, prev, type, len, i, sum;
	uint32_t done = FALSE;
	FSIZE_t after_sync;

	memset(&r, 0, sizeof(r));
	if (f_open(&r.fil, fname, FA_READ) != FR_OK) {
		return "CAN NOT OPEN THE CAS FILE";
	}
	while (!done && !err) {
		// leader, and anything else, up to the sync byte
		c = -1;
		do {
			prev = c;
			c = cas_getc(&r);
		} while (c >= 0 && c != CAS_SYNC);
		after_sync = cas_tell(&r);
		type = cas_getc(&r);
		len = cas_getc(&r);
		if (c < 0 || type < 0 || len < 0) {
			break;
		}
		sum = type + len;
		for (i = 0; i < len && (c = cas_getc(&r)) >= 0; i++) {
			block[i] = c;
			sum += c;
		}
		if (i < len) {
			break;		// cut short. Use what there is
		}
		if (cas_getc(&r) != (sum & 0xFF)) {
			if (prev != CAS_LEADER) {
				// a $3C in a gap, not a block after its leader. Look again from just after it
				f_lseek(&r.fil, after_sync);
				r.len = r.pos = 0;
				continue;
			}
			err = "CHECKSUM ERROR IN CAS FILE";
			break;
		}

		switch (type) {
		case CAS_BLOCK_NAME:
			if (header) {
				err = cas_finish(header, out);		// no EOF block before this one
			}
			header = 0;
			if (len < 15) {
				break;
			}
			// name (8), file type, ASCII flag, gap flag, exec address, load address
			if (block[8] == CAS_FILETYPE_BASIC && block[9] != 0) {
				*ascii_basic = TRUE;
			} else if (!want_basic && block[8] == CAS_FILETYPE_MC) {
				if (out + 7 > end) {
					err = "CAS FILE TOO BIG";
					break;
				}
				header = out;
				*out++ = 'M';
				*out++ = block[11];
				*out++ = block[12];
				*out++ = block[13];
				*out++ = block[14];
				out += 2;
			} else if (!want_basic && block[8] == CAS_FILETYPE_STATE) {
				if (out + 3 > end) {
					err = "CAS FILE TOO BIG";
					break;
				}
				header = out;
				*out++ = 'S';
				out += 2;
			} else if (want_basic && block[8] == CAS_FILETYPE_BASIC) {
				if (out + 3 > end) {
					err = "CAS FILE TOO BIG";
					break;
				}
				header = out;
				*out++ = 'B';
				out += 2;
			}
			break;
		case CAS_BLOCK_DATA:
			if (header) {
				if (out + len > end) {
					err = "CAS FILE TOO BIG";
					break;
				}
				memcpy(out, block, len);
				out += len;
			}
			break;
		case CAS_BLOCK_EOF:
			if (header) {
				err = cas_finish(header, out);
				header = 0;
				done = want_basic;		// only the first BASIC program
			}
			break;
		}
	}
	if (header && !err) {
		err = cas_finish(header, out);		// the file ends without an EOF block
	}
	f_close(&r.fil);
	*outp = out;
	return err;
}

// Turn fname into the list of things for the menu to load (see MENU_BUFFER_* in defines.h): the
// machine code files first, then the BASIC program. Returns 0, or the message it also left in out.
const char *load_cas(char *fname, uint8_t *out, uint8_t *end) {
	uint8_t *p = out;
	uint32_t ascii_basic = FALSE;
	const char *err;

	end--;			// room for the 0 at the end
	err = cas_pass(fname, FALSE, &p, end, &ascii_basic);
	if (!err) {
		err = cas_pass(fname, TRUE, &p, end, &ascii_basic);
	}
	if (!err && p == out) {
		err = ascii_basic ? "BASIC SAVED AS TEXT. NOT LOADED" : "NOTHING TO LOAD IN THE CAS FILE";
	}
	if (err) {
		out[0] = 'E';
		strcpy((char *) &out[1], err);
		return err;
	}
	*p = 0;
	return 0;
}

// Open path and walk readdir_menu() to the entry at ordinal. dir is left just after it, so
// readdir_image() carries on with the next rom or disk. *images_before is how many roms and
// disks came before it. Returns FR_OK with fno->fname[0] == 0 if there is no such entry.
FRESULT find_menu_entry(DIR *dir, char *path, uint32_t ordinal, FILINFO *fno, int32_t *images_before) {
	FRESULT res;
	uint32_t i;

	*images_before = 0;
	f_closedir(dir);
	res = f_opendir(dir, path);
	if (res != FR_OK) {
		return res;
	}
	for (i = 0; i <= ordinal; i++) {
		res = readdir_menu(dir, fno);
		if (res != FR_OK || fno->fname[0] == 0) {
			return res;
		}
		if (i < ordinal && is_image_file(fno)) {
			(*images_before)++;
		}
	}
	return FR_OK;
}

