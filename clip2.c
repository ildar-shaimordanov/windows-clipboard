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
#include <fcntl.h>
#include <windows.h>
#include <winuser.h>


#define HELP \
	"[ clp [OPTIONS] | ] ... [ | clp [OPTIONS] ]\n" \
	"\n" \
	"Copy data from and/or to the clipboard\n" \
	"\n" \
	"OPTIONS\n" \
	"\t-u\tdos2unix\n" \
	"\t-d\tunix2dos\n" \
	"\t-U\tunicode format (default)\n" \
	"\t-T\tplain text format\n"


enum {
	CM_AS_IS	= 0,
	CM_UNIX2DOS	= 1,
	CM_DOS2UNIX	= 2,
};


#define CHUNK_SIZE	0x10000


typedef struct chunk_tag {
	char buffer[CHUNK_SIZE];
	int capacity;
	struct chunk_tag *next;
} chunk;


void* chunkalloc() {
	chunk *buf = malloc(sizeof(chunk));
	if ( buf == NULL ) {
		fprintf(stderr, "Memory allocation error\n");
		exit(1);
	}
	buf->capacity = 0;
	buf->next = NULL;
	return buf;
}


int setclip(int conv_mode, int cb_format) {
	_setmode(_fileno(stdin), _O_BINARY);

	chunk *chunks = chunkalloc();
	chunk *head = chunks;

	DWORD cbSize = 0;
	DWORD readCount;
	char buffer[CHUNK_SIZE >> 1];

	while ( readCount = fread(buffer, 1, sizeof(buffer), stdin) ) {
		DWORD copyCount = 0;
		for (DWORD i = 0; i < readCount; i++) {
			char ch = buffer[i];
			if ( ch == '\r' && conv_mode != CM_AS_IS ) {
				continue;
			}
			if ( ch == '\n' && conv_mode == CM_UNIX2DOS ) {
				head->buffer[copyCount++] = '\r';
			}
			head->buffer[copyCount++] = ch;
		}

		cbSize += copyCount;
		head->capacity = copyCount;

		head->next = chunkalloc();
		head = head->next;
	}

	if ( ferror(stdin) ) {
		fprintf(stderr, "STDIN reading error\n");
		exit(1);
	}

	HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, cbSize + sizeof(wchar_t));
	LPBYTE pData = GlobalLock(hData);

	cbSize = 0;
	head = chunks;
	while ( head->capacity != 0 ) {
		CopyMemory(pData + cbSize, head->buffer, head->capacity);
		cbSize += head->capacity;
		chunk *prev = head;
		head = head->next;
		free(prev);
	}

	GlobalUnlock(hData);

	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(cb_format, hData);
	CloseClipboard();

	GlobalFree(hData);
}


int getclip(int conv_mode, int cb_format) {
	_setmode(_fileno(stdout), _O_BINARY);

	OpenClipboard(NULL);

	HANDLE hData = GetClipboardData(cb_format);
	if ( ! hData ) {
		return 0;
	}

	char *pData = (char *)GlobalLock(hData);

	char ch;
	while ( ch = *pData++ ) {
		if ( ch == '\r' && conv_mode != CM_AS_IS ) {
			continue;
		}
		if ( ch == '\n' && conv_mode == CM_UNIX2DOS ) {
			fputc('\r', stdout);
		}
		fputc(ch, stdout);
	}

	GlobalUnlock(hData);

	CloseClipboard();
}


int main(int argc, char **argv) {
	int conv_mode = CM_AS_IS;
	int cb_format = CF_UNICODETEXT;

	for (int i = 0; i < argc; i++) {
		char *arg = argv[i];
		if ( strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0 ) {
			printf(HELP);
			return 0;
		}
		if ( strcmp(arg, "-u") == 0 ) {
			conv_mode = CM_DOS2UNIX;
		}
		if ( strcmp(arg, "-d") == 0 ) {
			conv_mode = CM_UNIX2DOS;
		}
		if ( strcmp(arg, "-U") == 0 ) {
			cb_format = CF_UNICODETEXT;
		}
		if ( strcmp(arg, "-T") == 0 ) {
			cb_format = CF_TEXT;
		}
	}

	if ( ! _isatty(_fileno(stdin)) ) {
		// ... | clp
		setclip(conv_mode, cb_format);
	} else {
		// clp | ...
		// or simply output the clipboard
		getclip(conv_mode, cb_format);
	}
}
