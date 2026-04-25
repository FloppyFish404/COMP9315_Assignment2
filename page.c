// page.c ... functions on Pages
// part of Multi-attribute Linear-hashed Files
// Reading/writing pages into buffers and manipulating contents
// Credit: John Shepherd

#define _POSIX_C_SOURCE 200809L
#include "defs.h"
#include "page.h"

// internal representation of pages
struct PageRep {
	Offset free;   // offset within data[] of free space
	Offset ovflow; // Offset of overflow page (if any)
	Count ntuples; // #tuples in this page
	char data[1];  // start of data
};

// A Page is a chunk of memory containing PAGESIZE bytes
// It is implemented as a struct (free, ovflow, data[1])
// - free is the offset of the first byte of free space
// - ovflow is the page id of the next overflow page in bucket
// - data[] is a sequence of bytes containing tuples
// - each tuple is a sequence of chars terminated by '\0'
// - PageID values count # pages from start of file

// create a new initially empty page in memory
Page newPage()
{
	Page p = malloc(PAGESIZE);
	assert(p != NULL);
	p->free = 0;
	p->ovflow = NO_PAGE;
	p->ntuples = 0;
	Count hdr_size = 2*sizeof(Offset) + sizeof(Count);
	int dataSize = PAGESIZE - hdr_size;
	memset(p->data, 0, dataSize);
	return p;
}

// append a new Page to a file; return its PageID
PageID addPage(FILE *f)
{
	int ok = fseek(f, 0, SEEK_END);
	assert(ok == 0);
	int pos = ftell(f);
	assert(pos >= 0);
	PageID pid = pos/PAGESIZE;
	Page p = newPage();
	ok = putPage(f, pid, p);
	assert(ok == 0);
	return pid;
}

// remove the last Page from a file by truncating
void removePage(FILE *f)
{
	int ok = fseek(f, 0, SEEK_END);
	assert(ok == 0);
	int pos = ftell(f);
	assert(pos >= PAGESIZE);
	ok = ftruncate(fileno(f), pos - PAGESIZE);
	assert(ok == 0);
}

// fetch a Page from a file; allocate a memory buffer
Page getPage(FILE *f, PageID pid)
{
	assert(pid >= 0);
	Page p = malloc(PAGESIZE);
	assert(p != NULL);
	int ok = fseek(f, pid*PAGESIZE, SEEK_SET);
	assert(ok == 0);
	int n = fread(p, 1, PAGESIZE, f);
	assert(n == PAGESIZE);
	return p;
}

// write a Page to a file; release allocated buffer
Status putPage(FILE *f, PageID pid, Page p)
{
	assert(pid >= 0);
	int ok = fseek(f, pid*PAGESIZE, SEEK_SET);
	assert(ok == 0);
	int n = fwrite(p, 1, PAGESIZE, f);
	assert(n == PAGESIZE);
	free(p);
	return 0;
}

// insert a tuple into a page
// returns 0 status if successful
// returns -1 if not enough room
Status addToPage(Page p, Tuple t)
{
	int n = tupLength(t);
	char *c = p->data + p->free;
	Count hdr_size = 2*sizeof(Offset) + sizeof(Count);
	char *end = &p->data[PAGESIZE-hdr_size-1]; // end of page, reserved for page terminator
	// doesn't fit ... return fail code
	// assume caller will put it elsewhere
	if (c + n + 1 > end) return -1; 
	strcpy(c, t);
	p->free += n+1;
	p->ntuples++;
	return OK;
}

// delete all occurrences of tuple t from page, compact remaining tuples
Count deleteFromPage(Page p, Reln r, Tuple t)
{
	Count deleted = 0;
	char *read = p->data;
	char *write = p->data;
	char *end = p->data + p->free;

	while (read < end) {
		int len = tupLength(read) + 1;
		if (tupleMatch(r, t, read)) {
			deleted++;
		} else {
			if (write != read)
				memmove(write, read, len);
			write += len;
		}
		read += len;
	}

	if (deleted > 0) {
		Count hdr_size = 2*sizeof(Offset) + sizeof(Count);
		p->free = write - p->data;
		memset(write, 0, PAGESIZE - hdr_size - p->free);
		p->ntuples -= deleted;
	}
	return deleted;
}

// extract page info
char *pageData(Page p) { return p->data; }
Count pageNTuples(Page p) { return p->ntuples; }
Offset pageOvflow(Page p) { return p->ovflow; }
void pageSetOvflow(Page p, PageID pid) { p->ovflow = pid; }
Count pageFreeSpace(Page p) {
	Count hdr_size = 2*sizeof(Offset) + sizeof(Count);
	return (PAGESIZE-hdr_size-p->free);
}

