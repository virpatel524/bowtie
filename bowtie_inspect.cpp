/*
 * This source file is derived from Maq v0.6.6.  It is distributed
 * under the GNU GENERAL PUBLIC LICENSE v2 with no warranty.  See file
 * "COPYING" in this directory for the terms.
 */

#include <string>
#include <vector>
#include <iostream>
#include <getopt.h>
#include <seqan/find.h>

#include "endian_swap.h"
#include "ebwt.h"

using namespace std;
using namespace seqan;

static int showVersion = 0;  // just print version and quit?
static int verbose     = 0;  // be talkative
static int names_only  = 0;  // just print the sequence names in the index
static int across      = 60; // number of characters across in FASTA output

static const char *short_options = "vh?na:";

static struct option long_options[] = {
	{"verbose", no_argument,       0, 'v'},
	{"names",   no_argument,       0, 'n'},
	{"help",    no_argument,       0, 'h'},
	{"across",  required_argument, 0, 'a'},
	{0, 0, 0, 0} // terminator
};

/**
 * Print a summary usage message to the provided output stream.
 */
static void printUsage(ostream& out) {
	out
	<< "Usage: bowtie-inspect [options]* <ebwt_base>" << endl
	<< "  <ebwt_base>        ebwt filename minus trailing .1.ebwt/.2.ebwt" << endl
	<< "Options:" << endl
	<< "  -a/--across        number of characters across in FASTA output (default: 60)" << endl
	<< "  -n/--names         Print reference sequence names only" << endl
	<< "  -v/--verbose       verbose output (for debugging)" << endl
	<< "  -h/--help          print detailed description of tool and its options" << endl
	;
}

/**
 * Print a detailed usage message to the provided output stream.
 *
 * Manual text converted to C++ string with something like:
 * cat MANUAL  | head -415 | tail -340 | sed -e 's/\"/\\\"/g' | \
 *   sed -e 's/^/"/' | sed -e 's/$/\\n"/'
 */
static void printLongUsage(ostream& out) {
	out <<
	"\n"
	"LONG USAGE TODO\n"
	"\n"
	;
}

/**
 * Parse an int out of optarg and enforce that it be at least 'lower';
 * if it is less than 'lower', than output the given error message and
 * exit with an error and a usage message.
 */
static int parseInt(int lower, const char *errmsg) {
	long l;
	char *endPtr= NULL;
	l = strtol(optarg, &endPtr, 10);
	if (endPtr != NULL) {
		if (l < lower) {
			cerr << errmsg << endl;
			printUsage(cerr);
			exit(1);
		}
		return (int32_t)l;
	}
	cerr << errmsg << endl;
	printUsage(cerr);
	exit(1);
	return -1;
}

/**
 * Read command-line arguments
 */
static void parseOptions(int argc, char **argv) {
    int option_index = 0;
	int next_option;
	do {
		next_option = getopt_long(argc, argv, short_options, long_options, &option_index);
		switch (next_option) {
	   		case 'h': printLongUsage(cout); exit(0); break;
	   		case '?': printUsage(cerr); exit(1); break;
	   		case 'v': verbose = true; break;
			case 'n': names_only = true; break;
			case 'a': across = parseInt(1, "-a/--across arg must be at least 1"); break;
			case -1: break; /* Done with options. */
			case 0:
				if (long_options[option_index].flag != 0)
					break;
			default:
				cerr << "Unknown option: " << (char)next_option << endl;
				printUsage(cerr);
				exit(1);
		}
	} while(next_option != -1);
}

void print_fasta_record(ostream& fout, 
						const string& defline,
						const string& seq)
{
	fout << ">";
	fout << defline << endl;
	
	size_t i = 0;
	while (i + across < seq.length())
	{
		fout << seq.substr(i, across) << endl;
		i += across;
	}
	if (i < seq.length())
		fout << seq.substr(i) << endl;
}

template<typename TStr>
void print_index_sequences(ostream& fout, Ebwt<TStr>& ebwt)
{
	vector<string>* refnames = &(ebwt.refnames());
	
	TStr cat_ref;
	ebwt.restore(cat_ref);
	
	uint32_t curr_ref = 0xffffffff;
	string curr_ref_seq = "";
	uint32_t last_text_off = 0;
	size_t orig_len = seqan::length(cat_ref);
	uint32_t tlen = 0xffffffff;
	for(size_t i = 0; i < orig_len; i++) {
		uint32_t tidx = 0xffffffff;
		uint32_t textoff = 0xffffffff;
		tlen = 0xffffffff;
		
		ebwt.joinedToTextOff(1 /* qlen */, i, tidx, textoff, tlen, true);
		
		if (tidx != 0xffffffff && textoff < tlen)
		{
			if (curr_ref != tidx)
			{
				if (curr_ref != 0xffffffff)
				{
					// Add trailing gaps, if any exist
					if(curr_ref_seq.length() < tlen) {
						curr_ref_seq += string(tlen - curr_ref_seq.length(), 'N');
					}
					print_fasta_record(fout, (*refnames)[curr_ref], curr_ref_seq);
				}
				curr_ref = tidx;
				curr_ref_seq = "";
				last_text_off = 0;
			}
			
			if (textoff - last_text_off > 1)
				curr_ref_seq += string(textoff - last_text_off - (last_text_off ? 1 : 0), 'N');
			
			curr_ref_seq.push_back(getValue(cat_ref,i));
			last_text_off = textoff;
		}
	}
	if (curr_ref < refnames->size())
	{
		// Add trailing gaps, if any exist
		if(curr_ref_seq.length() < tlen) {
			curr_ref_seq += string(tlen - curr_ref_seq.length(), 'N');
		}
		print_fasta_record(fout, (*refnames)[curr_ref], curr_ref_seq);
	}
	
}

template<typename TStr>
void print_index_sequence_names(ostream& fout, Ebwt<TStr>& ebwt)
{
	vector<string>* p_refnames = &ebwt.refnames();
	if (!p_refnames)
		return;
	for (size_t i = 0; i < p_refnames->size() - 1; ++i)
	{
		string& name = (*p_refnames)[i];
		fout << name << endl;
	}
}

static char *argv0 = NULL;

template<typename TStr>
static void driver(const char * type,
                   const string& ebwtFileBase,
                   const string& query,
                   const vector<string>& queries)
{
	// Adjust
	string adjustedEbwtFileBase = adjustEbwtBase(argv0, ebwtFileBase, verbose);
	
	// Initialize Ebwt object and read in header
    Ebwt<TStr> ebwt(adjustedEbwtFileBase, -1, -1, verbose, false, false);
	ebwt.loadIntoMemory();
	
	if (names_only)
		print_index_sequence_names(cout, ebwt);
	else
		print_index_sequences(cout, ebwt);
	
	// Evict any loaded indexes from memory
	if(ebwt.isInMemory()) {
		ebwt.evictFromMemory();
	}
}

/**
 * main function.  Parses command-line arguments.
 */
int main(int argc, char **argv) {
	string ebwtFile;  // read serialized Ebwt from this file
	string query;   // read query string(s) from this file
	vector<string> queries;
	string outfile; // write query results to this file
	argv0 = argv[0];
	parseOptions(argc, argv);
	if(showVersion) {
		cout << argv0 << " version " << BOWTIE_VERSION << endl;
		cout << "Built on " << BUILD_HOST << endl;
		cout << BUILD_TIME << endl;
		cout << "Compiler: " << COMPILER_VERSION << endl;
		cout << "Options: " << COMPILER_OPTIONS << endl;
		cout << "Sizeof {int, long, long long, void*}: {" << sizeof(int)
		     << ", " << sizeof(long) << ", " << sizeof(long long)
		     << ", " << sizeof(void *) << "}" << endl;
		cout << "Source hash: " << EBWT_INSPECT_HASH << endl;
		return 0;
	}
	
	// Get input filename
	if(optind >= argc) {
		cerr << "No index name given!" << endl;
		printUsage(cerr);
		return 1;
	}
	ebwtFile = argv[optind++];
	
	// Optionally summarize
	if(verbose) {
		cout << "Input ebwt file: \"" << ebwtFile << "\"" << endl;
		cout << "Output file: \"" << outfile << "\"" << endl;
		cout << "Local endianness: " << (currentlyBigEndian()? "big":"little") << endl;
#ifdef NDEBUG
		cout << "Assertions: disabled" << endl;
#else
		cout << "Assertions: enabled" << endl;
#endif
	}
	driver<String<Dna, Packed<Alloc<> > > >("DNA", ebwtFile, query, queries);

	return 0;
}