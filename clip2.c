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
#include <Windows.h>
#include <winuser.h>

#define HELP \
	"[ clp [OPTIONS] | ] ... [ | clp [OPTIONS] ]\n" \
	"\n" \
	"Copy data from and/or to the clipboard\n" \
	"\n" \
	"OPTIONS\n" \
	"\t-u\tdos2unix\n" \
	"\t-d\tunix2dos\n" \
	"\t-U\tunicode format  (default)\n" \
	"\t-T\tplain text format\n"


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


#define BUFFER_SIZE	0x10000

typedef struct node_tag {
	char buffer[BUFFER_SIZE];
	int capacity;
	struct node_tag *next;
} node;

void* nodealloc() {
	node *buf = malloc(sizeof(node));
	if ( buf == NULL ) {
		fprintf(stderr, "Memory allocation error\n");
		exit(255);
	}
	return buf;
}

int setclip(int conv_mode, int io_mode, int cb_format) {
	_setmode(_fileno(stdin), io_mode);

	node *bufferList = nodealloc();
	bufferList->capacity = 0;
	node *head = bufferList;

	DWORD cbSize = 0;
	DWORD count;
	char buffer[BUFFER_SIZE >> 1];
	while ( ( count = fread(buffer, 1, sizeof(buffer), stdin) ) != 0 ) {
		DWORD j = 0;
		for (DWORD i = 0; i < count; i++) {
			char ch = buffer[i];
			if ( ch == '\r' && conv_mode != CM_AS_IS ) {
				continue;
			}
			if ( ch == '\n' && conv_mode == CM_UNIX2DOS ) {
				head->buffer[j++] = '\r';
			}
			head->buffer[j++] = ch;
		}
		count = j;

		cbSize += count;

		head->capacity = count;
		head->next = nodealloc();
		head->next->capacity = 0;

		head = head->next;
	}

	if ( ferror(stdin) ) {
		fprintf(stderr, "STDIN reading failure\n");
		exit(255);
	}

	HANDLE hgMem = GlobalAlloc(GMEM_MOVEABLE, cbSize + sizeof(char));
	LPBYTE lpData = GlobalLock(hgMem);

	cbSize = 0;
	head = bufferList;
	while ( head->capacity != 0 ) {
		CopyMemory(lpData + cbSize, head->buffer, head->capacity);
		cbSize += head->capacity;
		node *prev = head;
		head = head->next;
		free(prev);
	}

	GlobalUnlock(hgMem);

	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(cb_format, hgMem);
	CloseClipboard();

	GlobalFree(hgMem);
}

int getclip(int conv_mode, int io_mode, int cb_format) {
	OpenClipboard(NULL);

	HANDLE hData = GetClipboardData(cb_format);
	if ( ! hData ) {
		return 0;
	}

	char *pText = (char *)GlobalLock(hData);

	_setmode(_fileno(stdout), io_mode);

	char ch;
	while ( ( ch = *pText++ ) != 0 ) {
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
	int io_mode = _O_BINARY;
	int cb_format = CF_UNICODETEXT;

	for (int i = 0; i < argc; i++) {
		if ( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ) {
			printf(HELP);
			return 0;
		}
		if ( strcmp(argv[i], "-u") == 0 ) {
			conv_mode = CM_DOS2UNIX;
		}
		if ( strcmp(argv[i], "-d") == 0 ) {
			conv_mode = CM_UNIX2DOS;
		}
		if ( strcmp(argv[i], "-U") == 0 ) {
			io_mode = _O_BINARY;
			cb_format = CF_UNICODETEXT;
		}
		if ( strcmp(argv[i], "-T") == 0 ) {
			io_mode = _O_BINARY;
			cb_format = CF_TEXT;
		}
	}

	if ( ! _isatty(_fileno(stdin)) ) {
		// ... | clp
		setclip(conv_mode, io_mode, cb_format);
	} else {
		// clp | ...
		// or simply output the clipboard
		getclip(conv_mode, io_mode, cb_format);
	}
}
