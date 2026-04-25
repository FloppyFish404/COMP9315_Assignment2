// select.c ... select scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Selection objects
// by John Shepherd, July 2019

#include "defs.h"
#include "select.h"
#include "reln.h"
#include "tuple.h"
#include "bits.h"
#include "hash.h"

// A suggestion ... you can change however you like
#include <string.h>
#include <stdio.h>

struct SelectionRep {
	Reln    rel;       // need to remember Relation info
	Bits    known;     // the hash value from MAH
	Bits    unknown;   // the unknown bits from MAH
	Page    curpage;   // current page in scan
	int     is_ovflow; // are we in the overflow pages?
	Offset  curtupOffset;    // offset of current tuple within page

	//TODO: add any other fields you need to manage the scan
    Tuple qtuple;  //  query tuple for tupleMatch()
    Bits curBits;  // current bits used to search for bucket
    char *curptr;  // current position of tuple within page
    int pos[MAXBITS];  // position of unknown bits
    int k;  // no. of unknown bits in lower d bits
    int combo;  // current permutation of unknown bits (000 → 111)
    int maxBitCombo;  // total no. combos 2^k (e.g. k=3 → 8 combos: 0..7)
};


// take a query string (e.g. "1234,?,abc,?")
// set up a SelectionRep object for the scan

Selection startSelection(Reln r, char *q)
{
    Selection new = malloc(sizeof(struct SelectionRep));
    assert(new != NULL);
    // TODO:
	// Partial algorithm:
	// form known bits from known attributes
	// form unknown bits from '?' and '%' attributes
	// compute PageID of first page
	//   using known bits and first "unknown" value
	// set all values in SelectionRep object

    // MY CODE
    // fprintf(stderr, "Here1\n");
    
    // initialise scan
    new->rel = r;
    new->curtupOffset = 0;
    new->is_ovflow = 0;

    // PARSE INPUT
    int n = nattrs(r);
    char **vals = malloc(n * sizeof(char *));
    assert(vals != NULL);
    
    int start = 0;
    int len = strlen(q);
    int idx = 0;
    
    for (int i = 0; i <= len; i++) {
        if (q[i] == ',' || q[i] == '\0') {
            if (idx >= n) {
                // too many attrs
                freeVals(vals, idx);
                free(new);
                return NULL;
            }
    
            int field_len = i - start;
    
            vals[idx] = malloc(field_len + 1);
            assert(vals[idx] != NULL);
    
            strncpy(vals[idx], q + start, field_len);
            vals[idx][field_len] = '\0';
    
            idx++;
            start = i + 1;
        }
    }

    // check correct no. of attributes
    if (idx != n) {
        freeVals(vals, idx);
        free(new);
        return NULL;
    }

    Tuple qt = (Tuple) q;
    new->qtuple = qt;
    Bits h = tupleHash(r, qt);

    // set bits from unknown attributes in 'known' to 0, 
    // and mark as 1 in 'unknown' bitmask
    new->known = h;
    new->unknown = 0;
    ChVecItem *cv = chvec(r);
    for (int i = 0; i < MAXBITS; i++) {
        int attr = cv[i].att;
        if (strcmp(vals[attr], "?") == 0) {
            new->unknown = setBit(new->unknown, i);
            new->known = unsetBit(new->known, i);
        }
    }
    freeVals(vals, n);

    // get d lowest bits
    int d = depth(r);
    int sp = splitp(r);
    
    Bits initial = new->known;

    PageID pid = getLower(initial, d);
    int nbits = d;
    if (pid < sp) {
        pid = getLower(initial, d + 1);
        nbits = d + 1;
    }

    // find unknown bit positions in lower d bits
    new->k = 0;
    for (int i = 0; i < nbits; i++) {
        if (bitIsSet(new->unknown, i)) {
            new->pos[new->k++] = i;
        }
    }
    new->combo = 0;
    new->maxBitCombo = 1 << new->k;  // 2^k

    // get first bucket
    new->curpage = getPage(dataFile(r), pid);
    new->curptr = pageData(new->curpage);
    new->curBits = initial;
    
    return new;
    }

// get next tuple during a scan

Tuple getNextTuple(Selection q)
{
    // TODO:
	// Partial algorithm:
    // if (more tuples in current page)
    //    get next matching tuple from current page
    // else if (current page has overflow)
    //    move to overflow page
    //    grab first matching tuple from page
    // else
    //    move to "next" bucket
    //    grab first matching tuple from data page
    // endif
    // if (current page has no matching tuples)
    //    go to next page (try again)
    // endif

    // MY CODE
    while (1) {
        // fprintf(stderr, "Here2\n");

        // scan current page
        while (q->curtupOffset < pageNTuples(q->curpage)) {
        
            Tuple t = (Tuple)q->curptr;
        
            q->curptr += strlen(q->curptr) + 1;
            q->curtupOffset++;
        
            // fprintf(stderr, "checking %s\n", t);
            if (tupleMatch(q->rel, q->qtuple, t)) {
                return t;
            }
        }

        // scan any overflow pages
        PageID ov = pageOvflow(q->curpage);

        if (ov != NO_PAGE) {
            q->curpage = getPage(ovflowFile(q->rel), ov);
            q->curptr = pageData(q->curpage);
            q->curtupOffset = 0;
            continue;
        }

        // move to next bucket
        q->combo++;
        if (q->combo >= q->maxBitCombo) {
            return NULL;
        }

        // next combination of curBits
        Bits b = q->known;  // unknown bits initialised to 0
        for (int i = 0; i < q->k; i++) {
            if ((q->combo >> i) & 1) {
                b = setBit(b, q->pos[i]);
            }
        }
        q->curBits = b;
        /*  DEBUG
        printf("next curr bits:");
        for (int i = 31; i >= 0; i--) {
            printf("%d", (b >> i) & 1);
        }
        printf("\n");
        */

        // load page
        int d = depth(q->rel);
        int sp = splitp(q->rel);
        
        PageID pid = getLower(q->curBits, d);
        
        if (pid < sp) {
            pid = getLower(q->curBits, d+1);
        }
        q->curpage = getPage(dataFile(q->rel), pid);

        q->curptr = pageData(q->curpage);
        q->curtupOffset = 0;
        q->is_ovflow = 0;
    }
    return NULL;
}

// clean up a SelectionRep object and associated data

void closeSelection(Selection q)
{
    // TODO:

    // MY CODE
    free(q);
}

