// tuple.c ... functions on tuples
// part of Multi-attribute Linear-hashed Files
// by John Shepherd, July 2019

#include "defs.h"
#include "tuple.h"
#include "reln.h"
#include "hash.h"
#include "chvec.h"
#include "bits.h"
#include "util.h"

// return number of bytes/chars in a tuple

int tupLength(Tuple t)
{
	return strlen(t);
}

// reads/parses next tuple in input

Tuple readTuple(Reln r, FILE *in)
{
	char line[MAXTUPLEN];
	if (fgets(line, MAXTUPLEN-1, in) == NULL)
		return NULL;
	line[strlen(line)-1] = '\0';
	// count fields
	// cheap'n'nasty parsing
	char *c; int nf = 1;
	for (c = line; *c != '\0'; c++)
		if (*c == ',') nf++;
	// invalid tuple
	if (nf != nattrs(r)) return NULL;
	return copyString(line); // needs to be free'd sometime
}

// extract values into an array of strings

void tupleVals(Tuple t, char **vals)
{
	char *c = t, *c0 = t;
	int i = 0;
	for (;;) {
		while (*c != ',' && *c != '\0') c++;
		if (*c == '\0') {
			// end of tuple; add last field to vals
			vals[i++] = copyString(c0);
			break;
		}
		else {
			// end of next field; add to vals
			*c = '\0';
			vals[i++] = copyString(c0);
			*c = ',';
			c++; c0 = c;
		}
	}
}

// release memory used for separate attribute values

void freeVals(char **vals, int nattrs)
{
	int i;
    // release memory used for each attribute
	for (i = 0; i < nattrs; i++) free(vals[i]);
    // release memory used for pointer array
    free(vals);
}

// hash a tuple using the choice vector
// TODO: actually use the choice vector to make the hash

Bits tupleHash(Reln r, Tuple t)
{
	char buf[MAXBITS+5];  //*** for debug
	Count nvals = nattrs(r);
    char **vals = malloc(nvals*sizeof(char *));
    assert(vals != NULL);
    tupleVals(t, vals);

    // OLD CODE
    // Bits hash = hash_any((unsigned char *)vals[0],strlen(vals[0]));

    // MY CODE
    ChVecItem *cv = chvec(r);
    Bits hash = 0;

    // precompute attribute hashes
    Bits attr_hash[nvals];
    for (int i = 0; i < nvals; i++) {
        attr_hash[i] = hash_any((unsigned char *)vals[i], strlen(vals[i]));
    }
    
    // build final hash using choice vector
    for (int i = 0; i < MAXBITS; i++) {
        int a = cv[i].att;
        int b = cv[i].bit;
        Bits bit = (attr_hash[a] >> b) & 1;
        hash |= (bit << i);
    }
    //

	bitsString(hash,buf);  //*** for debug
	// printf("hash(%s) = %s\n", vals[0], buf);  //*** for debug
    freeVals(vals,nvals);
	return hash;
}

// compare two tuples (allowing for "unknown" values)
// TODO: actually compare values
Bool tupleMatch(Reln r, Tuple pt, Tuple t)
{
	Count na = nattrs(r);
	char **ptv = malloc(na*sizeof(char *));
	tupleVals(pt, ptv);
	char **v = malloc(na*sizeof(char *));
	tupleVals(t, v);
	Bool match = TRUE;
	// TODO: actually compare values
    // MY CODE
    for (int i = 0; i < na; i++) {
        if (strcmp(ptv[i], "?") != 0 &&
            strcmp(ptv[i], v[i]) != 0) {
            match = FALSE;
            break;
        }
    }
    //

	freeVals(ptv,na); freeVals(v,na);
	return match;
}

// puts printable version of tuple in user-supplied buffer

void tupleString(Tuple t, char *buf)
{
	strcpy(buf,t);
}

// release memory used for tuple
void freeTuple(Tuple t)
{
    if (t != NULL) {
        free(t);
    }
}
