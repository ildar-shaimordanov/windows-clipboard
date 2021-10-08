/* vi: set sw=4 ts=4: */
/*
 * Windows clipboard I/O implementation
 *
 * Initially it was implemented by @sindresorhus (Sindre Sorhus) as two
 * separate C-tools: "copy.c" to copy input to clipboard and "paste.c"
 * to paste from clipboard. Later it was totaly rewritten as a Rust
 * tool. This tool is only implemented on C and acts as a bi-directional
 * pipe tool: capture output of a previous command in pipe and copy data
 * to clipboard or forward clipboard content to another command in pipe
 * or simply display it.
 *
 * See for details:
 * https://github.com/sindresorhus/win-clipboard/tree/v0.2.0
 * MIT License
 *
 * Look closer if it can be adapted for BusyBox:
 * https://github.com/rmyorston/busybox-w32
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
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


void warn_no_memory_and_exit(long size) {
	fprintf(stderr, "Not enough memory for %d bytes\n", size);
	exit(1);
}


void warn_help_and_exit(int code) {
	fprintf(stderr, HELP);
	exit(code);
}


#define CHUNK_SIZE	0x10000
#define READ_SIZE	(CHUNK_SIZE >> 6)


typedef struct chunk_tag {
	char buffer[CHUNK_SIZE];
	int capacity;
	struct chunk_tag *next;
} chunk;


void* chunkalloc() {
	chunk *buf = malloc(sizeof(chunk));
	if ( ! buf ) {
		warn_no_memory_and_exit(sizeof(chunk));
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
	DWORD copyCount = 0;
	char buffer[READ_SIZE];

	while ( readCount = fread(buffer, 1, sizeof(buffer), stdin) ) {
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

		/*
		We have to be sure that when we've got and parsed the
		next portion of input data, we won't cross the edge of
		the current chunk. When we are asked to read UNIX-like
		EOLs ("\n") and convert them to DOS-like EOLs ("\r\n")
		the buffer grows. So we have to keep the double size of
		the read buffer for emergency. For example, we could
		be asked to read #READ_SIZE bytes of "\n" and convert
		them to "\r\n".
		*/
		if ( copyCount >= CHUNK_SIZE - READ_SIZE * 2 ) {
			cbSize += copyCount;
			head->capacity = copyCount;

			copyCount = 0;

			head->next = chunkalloc();
			head = head->next;
		}
	}

	if ( ferror(stdin) ) {
		fprintf(stderr, "STDIN reading error\n");
		exit(1);
	}

	/*
	The last portion was read, parsed and stored in the last chuck,
	but information wasn't updated in the variables properly while
	looping. We do it here out of the loop.
	*/
	cbSize += copyCount + sizeof(wchar_t);
	head->capacity = copyCount;

	HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, cbSize);
	if ( ! hData ) {
		warn_no_memory_and_exit(cbSize);
	}

	char *pData = GlobalLock(hData);

	head = chunks;
	while ( head ) {
		CopyMemory(pData, head->buffer, head->capacity);
		pData += head->capacity;
		chunk *prev = head;
		head = head->next;
		free(prev);
	}
	*(wchar_t *)pData = 0;

	GlobalUnlock(hData);

	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(cb_format, hData);
	CloseClipboard();

	GlobalFree(hData);
}


void getclip(int conv_mode, int cb_format) {
	_setmode(_fileno(stdout), _O_BINARY);

	OpenClipboard(NULL);

	HANDLE hData = GetClipboardData(cb_format);
	if ( ! hData ) {
		goto FINALLY;
	}

	char *pData = (char *)GlobalLock(hData);

	DWORD ch;
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

FINALLY:
	CloseClipboard();
}


int main(int argc, char **argv) {
	int conv_mode = CM_AS_IS;
	int cb_format = CF_UNICODETEXT;

	int ch;
	while ( ( ch = getopt(argc, argv, "udUTh") ) != -1 ) {
		switch (ch) {
		case 'u':
			conv_mode = CM_DOS2UNIX;
			break;
		case 'd':
			conv_mode = CM_UNIX2DOS;
			break;
		case 'U':
			cb_format = CF_UNICODETEXT;
			break;
		case 'T':
			cb_format = CF_TEXT;
			break;
		case 'h':
			warn_help_and_exit(0);
		default:
			warn_help_and_exit(1);
		}
	}

	if ( optind < argc ) {
		warn_help_and_exit(1);
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
