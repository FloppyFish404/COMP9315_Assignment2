// delete.c ... delete tuples from a relation
// part of Multi-attribute linear-hashed files
// Delete matching tuples from a named relation
// Usage:  ./delete  [-v]  RelName  
// followed by 'v1,v2,v3,v4,...' where any of the vi's can be "?" (unknown)

#include "defs.h"
#include "reln.h"
#include "tuple.h"

#define USAGE "./delete  [-v]  RelName"

// Main ... process args, delete tuples

int main(int argc, char **argv)
{
	Reln r;  // handle on the open relation
	Tuple t;  // tuple pointer
	char err[2*MAXERRMSG];  // buffer for error messages
	char tup[MAXTUPLEN];  // buffer for printable tuples
	int verbose;  // show extra info on delete progress
	char *rname;  // name of table/file


	// process command-line args

	if (argc < 2) fatal(USAGE);
    if (strcmp(argv[1], "-v") == 0)
		{ verbose = 1; rname = argv[2]; }
	else
		{ verbose = 0; rname = argv[1]; }


	// set up relation for reading/writing

	if (!existsRelation(rname)) {
		sprintf(err, "No such relation: %s",rname);
		fatal(err);
	}
	if ((r = openRelation(rname, "r+")) == NULL) {
		sprintf(err, "Can't open relation: %s",rname);
		fatal(err);
	}

	
	// read stdin and delete tuples

	while ((t = readTuple(r,stdin)) != NULL) {
		Count ndeleted = deleteFromRelation(r, t);
		tupleString(t,tup); // printable version
		if (verbose) printf("Deleted %d: %s\n", ndeleted, tup);
		free(t);
	}


	// clean up
	closeRelation(r);

	return 0;
}
