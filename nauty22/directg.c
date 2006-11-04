/* directg.c version 1.0; B D McKay, Oct 17, 2004 */

#define USAGE "directg [-q] [-u|-T|-G] [-o] [-e#|-e#:#] [infile [outfile]]"

#define HELPTEXT \
" Read undirected graphs and orient their edges in all possible ways.\n\
  Edges can be oriented in either or both directions (3 possibilities).\n\
  Isomorphic directed graphs derived from the same input are suppressed.\n\
  If the input graphs are non-isomorphic then the output graphs are also.\n\
\n\
   -e# | -e#:#  specify a value or range of the total number of arcs\n\
    -o     orient each edge in only one direction, never both\n\
\n\
    -T  use a simple text output format (nv ne edges) instead of digraph6\n\
    -G  like -T but includes group size as third item (if less than 10^10)\n\
	  The group size does not include exchange of isolated vertices.\n\
    -u  no output, just count them\n\
    -q  suppress auxiliary information\n"

/*************************************************************************/

#include "gtools.h"
#include "naugroup.h"

typedef struct
{
    long hi,lo;
} bigint;

#define ZEROBIG(big) big.hi = big.lo = 0L
#define ADDBIG(big,extra) if ((big.lo += (extra)) >= 1000000000L) \
    { ++big.hi; big.lo -= 1000000000L;}
#define PRINTBIG(file,big) if (big.hi == 0) \
 fprintf(file,"%ld",big.lo); else fprintf(file,"%ld%09ld",big.hi,big.lo)

static bigint nin,ngen,nout;

FILE *outfile;

#define MAXNV 128 
#define MAXNE 1024
static int v0[MAXNE],v1[MAXNE];
static int edgeno[MAXNV][MAXNV];

#define MAXME ((2*MAXNE+WORDSIZE-1)/WORDSIZE)

static set x[MAXME];
static int ix[2*MAXNE],nix;
static boolean first;
static int me;
static permutation lastreject[MAXNV];
static boolean lastrejok;
static int rejectlevel;
static unsigned long groupsize;
static unsigned long newgroupsize;
static boolean Gswitch;

/* #define GROUPTEST */
#ifdef GROUPTEST
static long long totallab;
#endif

/**************************************************************************/

void
writeautom(permutation *p, int n)
/* Called by allgroup. */
{
    int i;

    for (i = 0; i < n; ++i) printf(" %2d",p[i]);
    printf("\n");
}

/**************************************************************************/

static boolean
ismax(permutation *p, int n)
/* test if x^p <= x */
{
    int i,j,k;
    set px[MAXME];

    EMPTYSET(px,me);

    for (j = 0; j < nix; ++j)
    {
	i = ix[j];
        k = i >> 1;
        if (i & 1) ADDELEMENT(px,edgeno[p[v1[k]]][p[v0[k]]]);
        else       ADDELEMENT(px,edgeno[p[v0[k]]][p[v1[k]]]);

	if (px[0] > x[0])
	{
	    rejectlevel = k;
	    return FALSE;
	}
    }

    rejectlevel = MAXNE+1;

    if (px[0] < x[0]) return TRUE;

    for (i = 1; i < me; ++i)
	if      (px[i] > x[i]) return FALSE;
	else if (px[i] < x[i]) return TRUE;

    ++newgroupsize;
    return TRUE;
}

/**************************************************************************/

void
testmax(permutation *p, int n, int *abort)
/* Called by allgroup2. */
{
    int i,j,k;

    if (first)
    {                       /* only the identity */
	first = FALSE;
	return;
    }

    if (!ismax(p,n))
    {
	*abort = 1;
	for (i = 0; i < n; ++i) lastreject[i] = p[i];
	lastrejok = TRUE;
    }
}

/**************************************************************************/

static int
trythisone(grouprec *group, int ne, int n)
{
    int i,k;
    boolean accept;

    first = TRUE;

    ADDBIG(ngen,1);
    nix = ne;
    newgroupsize = 1;

    if (!group || groupsize == 1)
	accept = TRUE;
    else if (lastrejok && !ismax(lastreject,n))
	accept = FALSE;
    else if (lastrejok && groupsize == 2)
	accept = TRUE;
    else
    {
	newgroupsize = 1;
	if (allgroup2(group,testmax) == 0)
	    accept = TRUE;
        else
	    accept = FALSE;
    }

    if (accept)
    {

#ifdef GROUPTEST
	if (groupsize % newgroupsize != 0)
			gt_abort("group size error\n");
	totallab += groupsize/newgroupsize;
#endif

	ADDBIG(nout,1);

	if (outfile)
	{
	    fprintf(outfile,"%d %ld",n,ne);
	    if (Gswitch) fprintf(outfile," %lu",newgroupsize);

            for (i = -1; (i = nextelement(x,me,i)) >= 0; )
	    {
	        k = i >> 1;
	        if (i & 1) fprintf(outfile," %d %d",v1[k],v0[k]);
	        else       fprintf(outfile," %d %d",v0[k],v1[k]);
	    }
            fprintf(outfile,"\n");
	}
        return MAXNE+1;
    }
    else
	return rejectlevel;
}

/**************************************************************************/

static int
scan(int level, int ne, int minarcs, int maxarcs, int sofar,
	boolean oriented, grouprec *group, int n)
/* Main recursive scan; returns the level to return to. */
{
    int k,retlev;

    if (level == ne)
    {
	retlev = trythisone(group,sofar,n);
	return retlev;
    }

    if (oriented || sofar + 1 + 2*(ne - level - 1) >= minarcs)
    {
        k = 2*level;
	ADDELEMENT(x,k);
	ix[sofar] = k;
	retlev = scan(level+1,ne,minarcs,maxarcs,sofar+1,oriented,group,n);
	DELELEMENT(x,k);
        if (retlev < level) return retlev;
	++k;
	ADDELEMENT(x,k);
	ix[sofar] = k;
	retlev = scan(level+1,ne,minarcs,maxarcs,sofar+1,oriented,group,n);
	DELELEMENT(x,k);
        if (retlev < level) return retlev;
    }

    if (!oriented && sofar + 2 + ne - level - 1 <= maxarcs)
    {
        k = 2*level;
	ADDELEMENT(x,k);
	ADDELEMENT(x,k+1);
	ix[sofar] = k;
	ix[sofar+1] = k+1;
	retlev = scan(level+1,ne,minarcs,maxarcs,sofar+2,oriented,group,n);
	DELELEMENT(x,k+1);
	DELELEMENT(x,k);
        if (retlev < level) return retlev;
    }

    return level-1;
}

/**************************************************************************/

static void
direct(graph *g, long minarcs, long maxarcs, boolean oriented, int m, int n)
{
    static DEFAULTOPTIONS_GRAPH(options);
    statsblk(stats);
    setword workspace[100];
    grouprec *group;
    long ne;
    int i,j,k,j0,j1,deg;
    set *gi;
    int lab[MAXNV],ptn[MAXNV],orbits[MAXNV];
    set active[(MAXNV+WORDSIZE-1)/WORDSIZE];

    nauty_check(WORDSIZE,m,n,NAUTYVERSIONID);

    j0 = -1;  /* last vertex with degree 0 */
    j1 = n;   /* first vertex with degree > 0 */
 
    ne = 0;
    for (i = 0, gi = g; i < n; ++i, gi += m)
    {
	deg = 0;
	for (j = 0; j < m; ++j) deg += POPCOUNT(gi[j]);
	if (deg == 0) lab[++j0] = i;
	else          lab[--j1] = i;
	ne += deg;
    }
    ne /= 2;

    me = (2*ne + WORDSIZE - 1) / WORDSIZE;
    if (me == 0) me = 1;
    EMPTYSET(x,me);

    if (ne == 0 && minarcs <= 0)
    {
	trythisone(NULL,0,n);
	return;
    }

    if (oriented)
    {
        if (maxarcs < ne || minarcs > ne) return;
    }
    else
    {
        if (maxarcs < ne || minarcs > 2*ne) return;
    }

    if (n > MAXNV || ne > MAXNE)
    {
	fprintf(stderr,">E directg: MAXNV or MAXNE exceeded\n");
	exit(1);
    }

    for (i = 0; i < n; ++i) ptn[i] = 1;
    ptn[n-1] = 0;
    EMPTYSET(active,m);
    ADDELEMENT(active,0);

    for (i = 0; i <= j0; ++i)
    {
	if (i < n-1) ADDELEMENT(active,i+1);
	ptn[i] = 0;
    }

    options.defaultptn = FALSE;
    options.userautomproc = groupautomproc;
    options.userlevelproc = grouplevelproc;

    nauty(g,lab,ptn,active,orbits,&options,&stats,workspace,100,m,n,NULL);

    if (stats.grpsize2 == 0)
	groupsize = stats.grpsize1 + 0.1;
    else
	groupsize = 0;

    group = groupptr(FALSE);
    makecosetreps(group);

    k = 0;
    for (i = 0, gi = g; i < n; ++i, gi += m)
    {
        for (j = i; (j = nextelement(gi,m,j)) >= 0; )
	{
	    v0[k] = i;
	    v1[k] = j;
	    edgeno[i][j] = 2*k;
	    edgeno[j][i] = 2*k+1;
	    ++k;
	}
    }

    lastrejok = FALSE;

    scan(0,ne,minarcs,maxarcs,0,oriented,group,n);
}

/**************************************************************************/

main(int argc, char *argv[])
{
	graph *g;
	int m,n,codetype;
	int argnum,j,outcode;
	char *arg,sw,*fmt;
	boolean badargs;
	boolean Tswitch,uswitch,eswitch,qswitch,oswitch;
	long minarcs,maxarcs;
	double t;
	char *infilename,*outfilename;
	FILE *infile;
#if MAXN
	graph h[MAXN*MAXM];
#else
	DYNALLSTAT(graph,h,h_sz);
#endif

	HELP;

	nauty_check(WORDSIZE,1,1,NAUTYVERSIONID);

	Tswitch = Gswitch = FALSE;
	uswitch = eswitch = oswitch = qswitch = FALSE;
	infilename = outfilename = NULL;

	argnum = 0;
	badargs = FALSE;
	for (j = 1; !badargs && j < argc; ++j)
	{
	    arg = argv[j];
	    if (arg[0] == '-' && arg[1] != '\0')
	    {
		++arg;
		while (*arg != '\0')
		{
		    sw = *arg++;
		         SWBOOLEAN('o',oswitch)
		    else SWBOOLEAN('q',qswitch)
		    else SWBOOLEAN('u',uswitch)
		    else SWBOOLEAN('T',Tswitch)
		    else SWBOOLEAN('G',Gswitch)
		    else SWRANGE('e',":-",eswitch,minarcs,maxarcs,"directg -e")
		    else badargs = TRUE;
		}
	    }
	    else
	    {
		++argnum;
		if      (argnum == 1) infilename = arg;
	        else if (argnum == 2) outfilename = arg;
		else                  badargs = TRUE;
	    }
	}

	if (badargs || argnum > 2)
	{
	    fprintf(stderr,">E Usage: %s\n",USAGE);
	    GETHELP;
	    exit(1);
	}

	if ((Gswitch!=0) + (Tswitch!=0) + (uswitch!=0) >= 2)
	    gt_abort(">E directg: -G, -T and -u are incompatible\n");

	if (!Tswitch && !Gswitch && !uswitch)
	    gt_abort(
	      ">E directg: digraph6 output is unimplemented; use -T or -G\n");

	if (!eswitch)
	{
	    minarcs = 0;
	    maxarcs = NOLIMIT;
	}

	if (!qswitch)
	{
	    fprintf(stderr,">A directg");
	    if (eswitch || oswitch || uswitch)
		fprintf(stderr," -");
	    if (oswitch) fprintf(stderr,"o");
	    if (uswitch) fprintf(stderr,"u");
	    if (Tswitch) fprintf(stderr,"T");
	    if (eswitch)
		fprintf(stderr,"e%d:%d",minarcs,maxarcs);
	    if (argnum > 0) fprintf(stderr," %s",infilename);
	    if (argnum > 1) fprintf(stderr," %s",outfilename);
	    fprintf(stderr,"\n");
	    fflush(stderr);
	}

	if (infilename && infilename[0] == '-') infilename = NULL;
	infile = opengraphfile(infilename,&codetype,FALSE,1);
	if (!infile) exit(1);
	if (!infilename) infilename = "stdin";

	if (uswitch)
	    outfile = NULL;
	else
	{
	    if (!outfilename || outfilename[0] == '-')
	    {
	        outfilename = "stdout";
	        outfile = stdout;
	    }
	    else if ((outfile = fopen(outfilename,"w")) == NULL)
	    {
	        fprintf(stderr,"Can't open output file %s\n",outfilename);
	        gt_abort(NULL);
	    }
	}

	ZEROBIG(nin); ZEROBIG(ngen); ZEROBIG(nout);

	t = CPUTIME;
	while (TRUE)
	{
	    if ((g = readg(infile,NULL,0,&m,&n)) == NULL) break;
	    ADDBIG(nin,1);
	    direct(g,minarcs,maxarcs,oswitch,m,n);
	    FREES(g);
	}
	t = CPUTIME - t;

        if (!qswitch)
        {
            fprintf(stderr,">Z ");
            PRINTBIG(stderr,nin);
            fprintf(stderr," graphs read from %s",infilename);
         /* fprintf(stderr,"; ");
            PRINTBIG(stderr,ngen);
            fprintf(stderr,"; %lu digraphs tested",ngen); */
            fprintf(stderr,"; ");
            PRINTBIG(stderr,nout);
            if (!uswitch)
                fprintf(stderr," digraphs written to %s",outfilename);
            else
		fprintf(stderr," digraphs generated");
	    fprintf(stderr,"; %.2f sec\n",t);
	}

#ifdef GROUPTEST
	fprintf(stderr,"Group test = %lld\n",totallab);
#endif

	exit(0);
}
