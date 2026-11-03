// Autoboot for DragonDOS disks. When a disk is presented, look at what is on it, pick a command
// (RUN"M", BOOT, ...) and patch the DOS rom so that it types the command once it has started.
// See autoboot.s for the 6809 side.
#include "util.h"
#include "autoboot.h"
#include "autoboot_stub.h"

#define DIR_TRACK		20
#define DIR_FIRST_SECTOR	3
#define DIR_LAST_SECTOR		18
#define DIR_ENTRY_SIZE		25
#define DIR_ENTRIES_PER_SECTOR	10
#define DIR_DELETED		0x80
#define DIR_CONTINUATION	0x20
#define DIR_END			0x08
#define SECTOR_SIZE		256

#define AUTOBOOT_MAX_CMD	60
#define AUTOBOOT_OFF_FILE	"autoboot.off"

// Names that are usually the menu or the loader on a disk, best first. Each is looked for as a
// .BAS first, then as a .BIN
static const char *start_names[] = { "MENU", "M", "AUTO-RUN", "AUTOEXEC", "AUTORUN", "AUTO", 0 };
static const char *start_exts[] = { "BAS", "BIN", 0 };

typedef struct {
	char name[9];
	char ext[4];
} dos_file;

#define MAX_FILES	(DIR_ENTRIES_PER_SECTOR * (DIR_LAST_SECTOR - DIR_FIRST_SECTOR + 1))
static dos_file files[MAX_FILES];
static uint8_t sector[SECTOR_SIZE];

static FRESULT read_sector(FIL *fil, uint32_t header_length, uint32_t track_size, uint32_t track, uint32_t s) {
	UINT n;
	FRESULT res = f_lseek(fil, header_length + track * track_size + (s - 1) * SECTOR_SIZE);

	if (res == FR_OK) {
		res = f_read(fil, sector, SECTOR_SIZE, &n);
		if (res == FR_OK && n != SECTOR_SIZE) {
			res = FR_INT_ERR;
		}
	}
	return res;
}

// A name that is all printable and not blank. Skips the rubbish some directories have in them
static uint32_t copy_name(char *to, const uint8_t *from, uint32_t len) {
	uint32_t i;

	for (i = 0; i < len && from[i] != 0 && from[i] != ' '; i++) {
		if (from[i] < 0x21 || from[i] > 0x7E) {
			return FALSE;
		}
		to[i] = from[i];
	}
	to[i] = 0;
	return TRUE;
}

// The files in the DragonDOS directory. Returns how many
static uint32_t read_directory(FIL *fil, uint32_t header_length, uint32_t track_size) {
	uint32_t count = 0, s, e;
	uint8_t *entry;

	for (s = DIR_FIRST_SECTOR; s <= DIR_LAST_SECTOR; s++) {
		if (read_sector(fil, header_length, track_size, DIR_TRACK, s) != FR_OK) {
			return count;
		}
		for (e = 0; e < DIR_ENTRIES_PER_SECTOR; e++) {
			entry = &sector[e * DIR_ENTRY_SIZE];
			if (entry[0] & DIR_END) {
				return count;
			}
			if (entry[0] & (DIR_DELETED | DIR_CONTINUATION)) {
				continue;
			}
			if (copy_name(files[count].name, &entry[1], 8) && files[count].name[0] &&
			    copy_name(files[count].ext, &entry[9], 3)) {
				count++;
			}
		}
	}
	return count;
}

static int find_file(uint32_t count, const char *name, const char *ext) {
	uint32_t i;

	for (i = 0; i < count; i++) {
		if (strcasecmp(files[i].name, name) == 0 && strcasecmp(files[i].ext, ext) == 0) {
			return i;
		}
	}
	return -1;
}

// RUN"NAME" for BASIC, LOAD"NAME.BIN":EXEC for machine code
static void start_command(char *cmd, dos_file *f) {
	if (strcasecmp(f->ext, "BAS") == 0) {
		strcpy(cmd, "RUN\"");
		strcat(cmd, f->name);
		strcat(cmd, "\"");
	} else {
		strcpy(cmd, "LOAD\"");
		strcat(cmd, f->name);
		strcat(cmd, ".");
		strcat(cmd, f->ext);
		strcat(cmd, "\":EXEC");
	}
}

// Work out what to type for the disk. cmd is left empty for nothing
static void choose_command(char *cmd, FIL *fil, uint32_t header_length, uint32_t track_size) {
	uint32_t count, i, bas = 0, bin = 0;
	int f, only_bas = -1, only_bin = -1, pair = -1, pairs = 0;
	const char **name, **ext;

	cmd[0] = 0;
	count = read_directory(fil, header_length, track_size);

	// 1. a menu or loader with one of the usual names
	for (ext = start_exts; *ext; ext++) {
		for (name = start_names; *name; name++) {
			if ((f = find_file(count, *name, *ext)) >= 0) {
				start_command(cmd, &files[f]);
				return;
			}
		}
	}
	// 2. a boot disk (DragonDOS's BOOT looks for "OS" at the start of track 0 sector 3)
	if (read_sector(fil, header_length, track_size, 0, 3) == FR_OK && sector[0] == 'O' && sector[1] == 'S') {
		strcpy(cmd, "BOOT");
		return;
	}
	// 3. only one program, or one BASIC loader with its machine code (FOO.BAS and FOO.BIN)
	for (i = 0; i < count; i++) {
		if (strcasecmp(files[i].ext, "BAS") == 0) {
			bas++;
			only_bas = i;
			if (find_file(count, files[i].name, "BIN") >= 0) {
				pairs++;
				pair = i;
			}
		} else if (strcasecmp(files[i].ext, "BIN") == 0) {
			bin++;
			only_bin = i;
		}
	}
	if (bas == 1) {
		start_command(cmd, &files[only_bas]);
	} else if (bas == 0 && bin == 1) {
		start_command(cmd, &files[only_bin]);
	} else if (pairs == 1 && bas <= 2) {		// not one game picked out of a collection
		start_command(cmd, &files[pair]);
	} else if (count > 0) {
		strcpy(cmd, "DIR");		// can not tell. At least show what is there
	}
}

// image.vdk -> image.run, if there is one. Its first line is the command, an empty file turns
// autoboot off for that disk. Returns TRUE if there was one
static uint32_t read_run_file(char *cmd, const char *image_name) {
	static char run_name[FOLDER_PATH_LENGTH + 1 + FF_MAX_LFN + 1];
	FIL f;
	UINT n;
	char *dot, *p;

	if (strlen(image_name) >= sizeof(run_name)) {
		return FALSE;
	}
	strcpy(run_name, image_name);
	dot = strrchr(run_name, '.');
	if (!dot || strlen(dot) < 4) {
		return FALSE;
	}
	strcpy(dot, ".run");
	memset(&f, 0, sizeof(FIL));
	if (f_open(&f, run_name, FA_READ) != FR_OK) {
		return FALSE;
	}
	if (f_read(&f, cmd, AUTOBOOT_MAX_CMD, &n) != FR_OK) {
		n = 0;
	}
	f_close(&f);
	cmd[n] = 0;
	for (p = cmd; *p && *p != '\r' && *p != '\n'; p++);
	*p = 0;
	return TRUE;
}

void autoboot_patch(uint8_t *rom, FIL *fil, const char *image_name, uint32_t header_length, uint32_t track_size) {
	static char cmd[AUTOBOOT_MAX_CMD + 1];
	FILINFO fno;
	uint32_t i, at = 0;

	// DragonDOS (and DOSs like it): "DK", and the JMP $8371 to BASIC's OK at the end of its start up
	if (rom[0] != 'D' || rom[1] != 'K') {
		return;
	}
	for (i = 2; i < 0x100; i++) {
		if (rom[i] == 0x7E && rom[i + 1] == 0x83 && rom[i + 2] == 0x71) {
			at = i;
			break;
		}
	}
	if (!at || f_stat(AUTOBOOT_OFF_FILE, &fno) == FR_OK) {
		return;
	}
	if (!read_run_file(cmd, image_name)) {
		choose_command(cmd, fil, header_length, track_size);
	}
	if (cmd[0] == 0) {
		return;
	}

	memcpy(&rom[AUTOBOOT_STUB_ADDRESS - 0xC000], autoboot_stub, sizeof(autoboot_stub));
	strcpy((char *) &rom[AUTOBOOT_STUB_ADDRESS - 0xC000 + AUTOBOOT_CMD_OFFSET], cmd);
	strcat((char *) &rom[AUTOBOOT_STUB_ADDRESS - 0xC000 + AUTOBOOT_CMD_OFFSET], "\r");
	rom[at + 1] = AUTOBOOT_STUB_ADDRESS >> 8;
	rom[at + 2] = AUTOBOOT_STUB_ADDRESS & 0xFF;
}
