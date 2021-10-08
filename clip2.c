/* vi: set sw=4 ts=4: */
/*
 * Windows clipboard I/O implementation
 *
 * Some ideas were taken from win-clipboard
 * https://github.com/sindresorhus/win-clipboard/tree/v0.2.0
 * MIT License
 *
 * Copyright 2021 Ildar Shaimordanov
 */

#include <stdio.h>
#include <unistd.h>
// #include <io.h>
#include <fcntl.h>
#include <Windows.h>
#include <winuser.h>

#define HELP \
	"[ clp [-u|-d] | ] ... [ | clp [-u|-d] ]\n" \
	"\n" \
	"Copy data from and/or to the clipboard\n" \
	"\n" \
	"\t-u\tdos2unix\n" \
	"\t-d\tunix2dos\n"


int warn(char *str) {
	if ( str == NULL ) {
		str = "Something wrong\n";
	}
	fprintf(stderr, "%s", str);
}

int die(char *str) {
	if ( str == NULL ) {
		str = "Died!\n";
	}
	warn(str);
	exit(255);
}

int print(char *str) {
	printf("%s", str);
}


enum {
	CM_AS_IS	= 0
,	CM_UNIX2DOS	= 1
,	CM_DOS2UNIX	= 2
};


#define BUFFER_SIZE 1024

int setclip(int conv_mode) {
	HANDLE hData = GlobalAlloc(GMEM_FIXED, BUFFER_SIZE);
	if ( hData == NULL ) {
		die("Memory allocation error\n");
	}

	char *bufStart = (char *)hData;
	char *buffer = (char *)hData;

	EmptyClipboard();

	setmode(fileno(stdin), O_BINARY);

	while ( ! feof(stdin) ) {
		if ( buffer - bufStart + 1 >= BUFFER_SIZE ) {
			*buffer++ = '\0';
			SetClipboardData(CF_TEXT, hData);
			buffer = (char *)hData;
		}

		char ch = fgetc(stdin);
		if ( ch == '\r' && conv_mode != CM_AS_IS ) {
			continue;
		}
		if ( ch == '\n' && conv_mode == CM_UNIX2DOS ) {
			*buffer++ = '\r';
		}
		*buffer++ = ch;
	}

	if ( buffer - bufStart > 0 ) {
		*buffer++ = '\0';
		SetClipboardData(CF_TEXT, hData);
	}
}

int getclip(int conv_mode) {
	char *buffer = (char*)GetClipboardData(CF_TEXT);
	char ch;

	setmode(fileno(stdout), O_BINARY);

	while ( ( ch = *buffer++ ) != '\0' ) {
		if ( ch == '\r' && conv_mode != CM_AS_IS ) {
			continue;
		}
		if ( ch == '\n' && conv_mode == CM_UNIX2DOS ) {
			fputc('\r', stdout);
		}
		fputc(ch, stdout);
	}
}


int main(int argc, char **argv) {
	int conv_mode = CM_AS_IS;

	for (int i = 0; i < argc; i++) {
		if ( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ) {
			print(HELP);
			return 0;
		}
		if ( strcmp(argv[i], "-u") == 0 ) {
			conv_mode = CM_DOS2UNIX;
		}
		if ( strcmp(argv[i], "-d") == 0 ) {
			conv_mode = CM_UNIX2DOS;
		}
	}

	if ( ! OpenClipboard(NULL) ) {
		die("Unable to open clipboard\n");
	}

	if ( ! isatty(fileno(stdin)) ) {
		// ... | clp
		setclip(conv_mode);
	} else {
		// clp | ...
		// or simply output the clipboard
		getclip(conv_mode);
	}

	CloseClipboard();
}
