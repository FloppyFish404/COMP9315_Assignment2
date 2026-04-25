// reln.c ... functions on Relations
// part of Multi-attribute Linear-hashed Files
// Credit: John Shepherd

#include "defs.h"
#include "reln.h"
#include "page.h"
#include "tuple.h"
#include "chvec.h"
#include "bits.h"
#include "hash.h"
#include "select.h"

#define HEADERSIZE (6*sizeof(Count)+sizeof(Offset)+sizeof(PageID))

struct RelnRep {
	Count  nattrs; // number of attributes
	Count  depth;  // depth of main data file
	Offset sp;     // split pointer
    Count  npages; // number of main data pages
    Count  ntups;  // total number of tuples
	PageID ovflowFreeList;  // head of overflow file freelist
	Count  insertsSinceSplit;  // inserts since last split
	Count  deletesSinceMerge;  // deletes since last merge
	ChVec  cv;     // choice vector
	char   mode;   // open for read/write
	FILE  *info;   // handle on info file
	FILE  *data;   // handle on data file
	FILE  *ovflow; // handle on ovflow file
};

// TODO: reuse the page from the freelist
// only add new page to ovflow file when the freelist is empty
PageID allocOvflowPage(Reln r)
{
    if (r->ovflowFreeList != NO_PAGE) {
        PageID pid = r->ovflowFreeList;
        fprintf(stderr, "ALLOC from freelist ov%d\n", pid);

        Page p = getPage(r->ovflow, pid);
        fprintf(stderr, "7 REUSE ov%d had next %d BEFORE reset\n", pid, pageOvflow(p));
        r->ovflowFreeList = pageOvflow(p);

        fprintf(stderr, "FREE PTR %p (context)\n", (void *)p);
        free(p);
        Page fresh = newPage();
        putPage(r->ovflow, pid, fresh);
        return pid;
    }
	return addPage(r->ovflow);
}

// TODO: add page to the freelist
void freeOvflowPage(Reln r, PageID pid)
{
    fprintf(stderr, "FREE ov%d -> next %d\n", pid, r->ovflowFreeList);
    Page p = newPage();

    pageSetOvflow(p, r->ovflowFreeList); // link to old head
    r->ovflowFreeList = pid;

    putPage(r->ovflow, pid, p);  // frees page
}

// create a new relation (three files)

Status newRelation(char *name, Count nattrs, Count npages, Count d, char *cv)
{
    char fname[MAXFILENAME];
	Reln r = malloc(sizeof(struct RelnRep));
	r->nattrs = nattrs; r->depth = d; r->sp = 0;
	r->npages = npages; r->ntups = 0;
	r->ovflowFreeList = NO_PAGE;
	r->insertsSinceSplit = 0; r->deletesSinceMerge = 0;
	r->mode = 'w';
	assert(r != NULL);
	if (parseChVec(r, cv, r->cv) != OK) return ~OK;
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,"w");
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,"w");
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,"w");
	assert(r->ovflow != NULL);
	int i;
	for (i = 0; i < npages; i++) addPage(r->data);
	closeRelation(r);
	return 0;
}

// check whether a relation already exists

Bool existsRelation(char *name)
{
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	FILE *f = fopen(fname,"r");
	if (f == NULL)
		return FALSE;
	else {
		fclose(f);
		return TRUE;
	}
}

// set up a relation descriptor from relation name
// open files, reads information from rel.info

Reln openRelation(char *name, char *mode)
{
	Reln r;
	r = malloc(sizeof(struct RelnRep));
	assert(r != NULL);
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,mode);
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,mode);
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,mode);
	assert(r->ovflow != NULL);
	// Naughty: assumes Count, Offset, PageID are the same size
	int n = fread(r, sizeof(Count), 8, r->info);
	assert(n == 8);
	n = fread(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
	assert(n == MAXCHVEC);
	r->mode = (mode[0] == 'w' || mode[1] =='+') ? 'w' : 'r';
	return r;
}

// release files and descriptor for an open relation
// copy latest information to .info file

void closeRelation(Reln r)
{
	// make sure updated global data is put in info
	// Naughty: assumes Count and Offset are the same size
	if (r->mode == 'w') {
		fseek(r->info, 0, SEEK_SET);
		// write out core relation info (#attr,#pages,d,sp,ntups,freelists)
		int n = fwrite(r, sizeof(Count), 8, r->info);
		assert(n == 8);
		// write out choice vector
		n = fwrite(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
		assert(n == MAXCHVEC);
	}
	fclose(r->info);
	fclose(r->data);
	fclose(r->ovflow);
	free(r);
}

// MY CODE
int splitThreshold(Reln r) {
    return 1024 / (10 * nattrs(r));
}

void insertIntoBucket(Reln r, Tuple *arr, int n, PageID pid) {
    Page pg = getPage(dataFile(r), pid);
    PageID curPid = pid;
    int isDataPage = 1;

    for (int i = 0; i < n; i++) {
        Tuple t = arr[i];

        // find free space to insert 
        while (addToPage(pg, t) != OK) {

            PageID next = pageOvflow(pg);

            if (next == NO_PAGE) {
                // create new overflow page
                PageID newov = addPage(r->ovflow);
                pageSetOvflow(pg, newov);

                // write current page before moving on
                if (isDataPage)
                    putPage(dataFile(r), curPid, pg);
                else
                    putPage(ovflowFile(r), curPid, pg);

                // switch to new overflow page
                pg = getPage(ovflowFile(r), newov);
                curPid = newov;
                isDataPage = 0;
            } else {
                // move to existing overflow page
                if (isDataPage)
                    putPage(dataFile(r), curPid, pg);
                else
                    putPage(ovflowFile(r), curPid, pg);

                pg = getPage(ovflowFile(r), next);
                curPid = next;
                isDataPage = 0;
            }
        }
    }

    // final write
    if (isDataPage)
        putPage(dataFile(r), curPid, pg);
    else
        putPage(ovflowFile(r), curPid, pg);
}

void splitBucket(Reln r, PageID sp) {

    // empty old bucket
    Tuple *tuples = NULL;
    int ntuples = 0;
    int capacity = 0;
    
    Page primary = getPage(dataFile(r), sp);
    Page cur = primary;
    PageID ov = pageOvflow(primary);

    while (1) {
        char *ptr = pageData(cur);
        int n = pageNTuples(cur);
    
        for (int i = 0; i < n; i++) {
            if (ntuples == capacity) {
                // dynamically double memory as needed
                capacity = (capacity == 0) ? 16 : capacity * 2;  
                tuples = realloc(tuples, capacity * sizeof(Tuple));
                assert(tuples != NULL);
            }
    
            tuples[ntuples++] = strdup(ptr);
            ptr += strlen(ptr) + 1;
        }
    
        // scan overflow
        if (ov == NO_PAGE) {
            fprintf(stderr, "1 FREE PTR %p (context)\n", (void *)cur);
            if (cur != primary) free(cur);
            break;
        }
    
        Page next = getPage(ovflowFile(r), ov);
        PageID nextOv = pageOvflow(next);

        fprintf(stderr, "2 FREE PTR %p (context)\n", (void *)cur);
        if (cur != primary) free(cur);
        cur = next;
        ov = nextOv;
    }

    // 'clean' old bucket
        // free all overflow pages
    ov = pageOvflow(primary);
    while (ov != NO_PAGE) {
        Page p = getPage(ovflowFile(r), ov);
        PageID next = pageOvflow(p);
        pageSetOvflow(p, NO_PAGE);  // break overflow link

        freeOvflowPage(r, ov);
        fprintf(stderr, "3 FREE PTR %p (context)\n", (void *)p);
        free(p);

        ov = next;
    }
        // reset old primary page
    Page empty = newPage();
    putPage(dataFile(r), sp, empty);

    // create new bucket
    int d = depth(r);
    PageID newpid = addPage(dataFile(r));  // id = sp + (1 << d)
    r->npages++;
    
    PageID oldpid = sp;

    // rehash and partition tuples into 2 lists
    Tuple *oldTuples = NULL;
    Tuple *newTuples = NULL;
    int nOld = 0, nNew = 0;
    int capOld = 0, capNew = 0;
    
    for (int i = 0; i < ntuples; i++) {
        Tuple t = tuples[i];
    
        Bits h = tupleHash(r, t);
        PageID p = getLower(h, d+1);
    
        if (p == oldpid) {
            if (nOld == capOld) {
                capOld = (capOld == 0) ? 16 : capOld * 2;
                oldTuples = realloc(oldTuples, capOld * sizeof(Tuple));
                assert(oldTuples != NULL);
            }
            oldTuples[nOld++] = t;
        } else {
            if (nNew == capNew) {
                capNew = (capNew == 0) ? 16 : capNew * 2;
                newTuples = realloc(newTuples, capNew * sizeof(Tuple));
                assert(newTuples != NULL);
            }
            newTuples[nNew++] = t;
        }
    }

    // write tuple partitions to buckets
    insertIntoBucket(r, oldTuples, nOld, oldpid);
    insertIntoBucket(r, newTuples, nNew, newpid);

    for (int i = 0; i < ntuples; i++) {
        free(tuples[i]);
    }
    free(tuples);
    free(oldTuples);
    free(newTuples);
}

void maybeSplit(Reln r) {
    if (r->insertsSinceSplit >= splitThreshold(r)) {
        splitBucket(r, r->sp);

        r->sp++;
        if (r->sp == (1 << r->depth)) {
            r->sp = 0;
            r->depth++;
        }

        r->insertsSinceSplit = 0;
    }
}

// insert a new tuple into a relation
// returns index of bucket where inserted
// - index always refers to a primary data page
// - the actual insertion page may be either a data page or an overflow page
// returns NO_PAGE if insert fails completely
// TODO: include splitting and file expansion
PageID addToRelation(Reln r, Tuple t)
{
	Bits h, p;
 	// char buf[MAXBITS+5]; //*** for debug
	h = tupleHash(r,t);
	if (r->depth == 0)
		p = 0;  // start from 0
	else {
		p = getLower(h, r->depth);
		if (p < r->sp) p = getLower(h, r->depth+1);
	}
	// bitsString(h,buf); printf("hash = %s\n",buf); //*** for debug
	// bitsString(p,buf); printf("page = %s\n",buf); //*** for debug
	Page pg = getPage(r->data,p);
	if (addToPage(pg,t) == OK) {
		putPage(r->data,p,pg);
		r->ntups++;
		r->insertsSinceSplit++;
        maybeSplit(r);
		return p;
	}
	// primary data page full
	if (pageOvflow(pg) == NO_PAGE) {
		// add first overflow page in chain
		PageID newp = allocOvflowPage(r);
		pageSetOvflow(pg,newp);
		putPage(r->data,p,pg);
		Page newpg = getPage(r->ovflow,newp);
		// can't add to a new page; we have a problem
		if (addToPage(newpg,t) != OK) return NO_PAGE;
		putPage(r->ovflow,newp,newpg);
		r->ntups++;
		r->insertsSinceSplit++;
        maybeSplit(r);  // MY CODE
		return p;
	}
	else {
		// scan overflow chain until we find space
		// worst case: add new ovflow page at end of chain
		Page ovpg, prevpg = NULL;
		PageID ovp, prevp = NO_PAGE;
		ovp = pageOvflow(pg);
		free(pg);
		while (ovp != NO_PAGE) {
            // DEBUG
            static int guard = 0;
            guard++;
            if (guard > 1000) {
                fprintf(stderr, "🔥 LOOP DETECTED in overflow scan at ov%d\n", ovp);
                exit(1);
            }
            //
			ovpg = getPage(r->ovflow, ovp);
            fprintf(stderr, "VISIT ov%d (ptr=%p)\n", ovp, (void*)ovpg);
            fprintf(stderr, "4 NEXT of ov%d = %d\n", ovp, pageOvflow(ovpg));
            if (pageOvflow(ovpg) == ovp) {
                fprintf(stderr, "5 💀 SELF LOOP at ov%d\n", ovp);
                exit(1);
            }
			if (addToPage(ovpg,t) != OK) {
			        if (prevpg != NULL) free(prevpg);
				prevp = ovp; prevpg = ovpg;
				ovp = pageOvflow(ovpg);
			}
			else {
				if (prevpg != NULL) free(prevpg);
				putPage(r->ovflow,ovp,ovpg);
				r->ntups++;
				r->insertsSinceSplit++;
                maybeSplit(r);  // MY CODE
				return p;
			}
		}
		// all overflow pages are full; add another to chain
		// at this point, there *must* be a prevpg
		assert(prevpg != NULL);
		// make new ovflow page
		PageID newp = allocOvflowPage(r);
		// insert tuple into new page
		Page newpg = getPage(r->ovflow,newp);
        if (addToPage(newpg,t) != OK) return NO_PAGE;
        putPage(r->ovflow,newp,newpg);
		// link to existing overflow chain
		pageSetOvflow(prevpg,newp);
		putPage(r->ovflow,prevp,prevpg);
        r->ntups++;
		r->insertsSinceSplit++;
        maybeSplit(r);  // MY CODE
		return p;
	}
	return NO_PAGE;
}

// TODO: delete all matching tuples from a relation
// t may contain unknown attributes (denoted by "?")
// returns the number of tuples deleted
Count deleteFromRelation(Reln r, Tuple t)
{
    Count totalDeleted = 0;

    // PARSE INPUT
    int n = nattrs(r);
    char **vals = malloc(n * sizeof(char *));
    assert(vals != NULL);

    int start = 0;
    int len = strlen(t);
    int idx = 0;

    for (int i = 0; i <= len; i++) {
        if (t[i] == ',' || t[i] == '\0') {
            if (idx >= n) {  
                // too many attrs
                freeVals(vals, idx);
                return 0;
            }

            int field_len = i - start;
            vals[idx] = malloc(field_len + 1);
            assert(vals[idx] != NULL);

            strncpy(vals[idx], t + start, field_len);
            vals[idx][field_len] = '\0';

            idx++;
            start = i + 1;
        }
    }

    // check correct no. of attributes
    if (idx != n) {
        freeVals(vals, idx);
        return 0;
    }

    Bits h = tupleHash(r, t);

    // set bits from unknown attribute in known to 0,
    // and mark as 1 un 'unknown' bitmask
    Bits known = h;
    Bits unknown = 0;
    ChVecItem *cv = chvec(r);
    for (int i = 0; i < MAXBITS; i++) {
        int attr = cv[i].att;
        if (strcmp(vals[attr], "?") == 0) {
            unknown = setBit(unknown, i);
            known = unsetBit(known, i);
        }
    }

    freeVals(vals, n);

    // get d lowest bits
    int d = depth(r);
    int sp = splitp(r);

    int nbits = d;
    if (getLower(known, d) < sp) {
        nbits = d + 1;
    }

    // find unknown bit postitions in lower d bits
    int pos[MAXBITS];
    int k = 0;
    for (int i = 0; i < nbits; i++) {
        if (bitIsSet(unknown, i)) {
            pos[k++] = i;
        }
    }

    int maxBitCombo = 1 << k;  // 2^k

    // DELETION SCAN
    for (int combo = 0; combo < maxBitCombo; combo++) {
        
        Bits b = known;
    
        for (int i = 0; i < k; i++) {
            if ((combo >> i) & 1) {
                b = setBit(b, pos[i]);
            }
        }

        PageID pid = getLower(b, d);
        if (pid < sp) pid = getLower(b, d + 1);

        Page curpage = getPage(dataFile(r), pid);
        int isDataPage = 1;
        PageID ov = NO_PAGE;
        while (1) {

            Count deleted = deleteFromPage(curpage, r, t);
            totalDeleted += deleted;
        
            // write page after deletions
            if (isDataPage) {
                putPage(dataFile(r), pid, curpage);
            } else {
                putPage(ovflowFile(r), ov, curpage);
            }
        
            // delete from any overflow pages
            PageID nextOv = pageOvflow(curpage);
            if (nextOv == NO_PAGE) break;
            ov = nextOv;
            curpage = getPage(ovflowFile(r), nextOv);
            isDataPage = 0;
        }
    }
    r->ntups -= totalDeleted;
    r->deletesSinceMerge += totalDeleted;

    return totalDeleted;
}


// external interfaces for Reln data

FILE *dataFile(Reln r) { return r->data; }
FILE *ovflowFile(Reln r) { return r->ovflow; }
Count nattrs(Reln r) { return r->nattrs; }
Count npages(Reln r) { return r->npages; }
Count ntuples(Reln r) { return r->ntups; }
Count depth(Reln r)  { return r->depth; }
Count splitp(Reln r) { return r->sp; }
ChVecItem *chvec(Reln r)  { return r->cv; }


// displays info about open Reln

void relationStats(Reln r)
{
	printf("Global Info:\n");
	printf("#attrs:%d  #pages:%d  #tuples:%d  d:%d  sp:%d\n",
	       r->nattrs, r->npages, r->ntups, r->depth, r->sp);
	printf("insertsSinceSplit:%d  deletesSinceMerge:%d\n",
	       r->insertsSinceSplit, r->deletesSinceMerge);
	printf("Overflow Free list: ");
	if (r->ovflowFreeList == NO_PAGE) {
		printf("(empty)");
	} else {
		PageID fid = r->ovflowFreeList;
		while (fid != NO_PAGE) {
			printf("ov%d", fid);
			Page fp = getPage(r->ovflow, fid);
			fid = pageOvflow(fp);
			free(fp);
			if (fid != NO_PAGE) printf(" -> ");
		}
	}
	putchar('\n');
	printf("Choice vector\n");
	printChVec(r->cv);
	printf("Bucket Info:\n");
	printf("%-4s %s\n","#","Info on pages in bucket");
	printf("%-4s %s\n","","(pageID,#tuples,freebytes,ovflow)");
	for (Offset pid = 0; pid < r->npages; pid++) {
		printf("[%2d]  ",pid);
		Page p = getPage(r->data, pid);
		Count ntups = pageNTuples(p);
		Count space = pageFreeSpace(p);
		Offset ovid = pageOvflow(p);
		printf("(d%d,%d,%d,%d)",pid,ntups,space,ovid);
		free(p);
		while (ovid != NO_PAGE) {
			Offset curid = ovid;
			p = getPage(r->ovflow, ovid);
			ntups = pageNTuples(p);
			space = pageFreeSpace(p);
			ovid = pageOvflow(p);
			printf(" -> (ov%d,%d,%d,%d)",curid,ntups,space,ovid);
			free(p);
		}
		putchar('\n');
	}
}
