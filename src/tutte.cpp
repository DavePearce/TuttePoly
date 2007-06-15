#include <iostream>
#include <iomanip>
#include <fstream>
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <csignal>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"
#include "eval_simple_fours.hpp" // auto-generated solutions for simple graphs of size 4
#include "eval_simple_fives.hpp" // auto-generated solutions for simple graphs of size 5
#include "cache/simple_cache.hpp"
#include "misc/biguint.hpp"
#include "misc/safe_arithmetic.hpp"

#include <set>

using namespace std;

// ---------------------------------------------------------------
// User-Defined Types
// ---------------------------------------------------------------

class my_timer {
private:
  struct timeval _start;
public:
  my_timer(void) {
    gettimeofday(&_start,NULL);
  }

  double elapsed(void) {
    struct timeval tmp;
    gettimeofday(&tmp,NULL); 
    double end = tmp.tv_sec + (tmp.tv_usec / 1000000.0);
    double start = _start.tv_sec + (_start.tv_usec / 1000000.0);    
    return end - start;
  }  
};

// ---------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------

typedef enum { RANDOM, MAXIMISE_DEGREE, MINIMISE_DEGREE, MAXIMISE_MDEGREE, MINIMISE_MDEGREE, MINIMISE_SDEGREE, VERTEX_ORDER } EDGE_SELECTION_HEURISTIC;

unsigned int resize_stats = 0;
unsigned long num_steps = 0;
unsigned long old_num_steps = 0;
unsigned int small_graph_threshold = 5;
EDGE_SELECTION_HEURISTIC edge_selection_heuristic = MINIMISE_DEGREE;
unsigned int xml_id = 2;
simple_cache cache(1024*1024,100);
static bool status_flag=false;
static bool xml_flag=false;

void print_status();

// ---------------------------------------------------------------
// Method Bodies
// ---------------------------------------------------------------

/* XML output methods.  Currently needed to interface with the visualisation
 * tool being developed by Bennett Thompson.
 */

void write_xml_start() {
  cout << "<object-stream>" << endl;
}

void write_xml_end() {
  cout << "</object-stream>" << endl;
}

template<class G>
void write_xml_match(unsigned int my_id, G const &graph) {
  cout << "<graphnode>" << endl;
  cout << "<id>" << my_id << "</id>" << endl;
  cout << "<vertices>" << graph.num_vertices() << "</vertices>" << endl;
  cout << "<edges>" << graph.num_edges() << "</edges>" << endl;
  cout << "<match>1</match>" << endl;
  cout << "</graphnode>" << endl;
}

template<class G>
void write_xml_nonleaf(unsigned int my_id, int left_id, int right_id, G const &graph) {
  cout << "<graphnode>" << endl;
  cout << "<id>" << my_id << "</id>" << endl;
  cout << "<vertices>" << graph.num_vertices() << "</vertices>" << endl;
  cout << "<edges>" << graph.num_edges() << "</edges>" << endl;
  cout << "<left>" << left_id << "</left>" << endl;
  cout << "<right>" << right_id << "</right>" << endl;
  cout << "</graphnode>" << endl;
}

template<class G>
void write_xml_leaf(unsigned int my_id, G const &graph) {
  cout << "<graphnode>" << endl;
  cout << "<id>" << my_id << "</id>" << endl;
  cout << "<vertices>" << graph.num_vertices() << "</vertices>" << endl;
  cout << "<edges>" << graph.num_edges() << "</edges>" << endl;
  cout << "</graphnode>" << endl;
}

/* This method determines which edge is chosen to delete contract upon.
 * It is key to how the algorithm operates!
 */

template<class G>
typename G::edge_t select_nontree_edge(G graph) {
  // assumes this graph is NOT a tree 
  unsigned int best(0);
  unsigned int V(graph.num_vertices());
  unsigned int rcount(0);
  unsigned int rtarget(0);
  typename G::edge_t r(-1,-1,-1);
  
  if(edge_selection_heuristic == RANDOM) {
    unsigned int nedges = graph.num_edges() - graph.num_spanning_edges();
    rtarget = (unsigned int) (((double) nedges*rand()) / (1.0+RAND_MAX));
  }

  for(typename G::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
    for(typename G::edge_iterator j(graph.begin_edges(*i));
	j!=graph.end_edges(*i);++j) {	
      unsigned int head = *i;
      unsigned int tail = j->first;
      
      if(head < tail) { // to avoid duplicates
	unsigned int count = j->second;
	// check whether edge on spanning tree
	if(graph.on_spanning_tree(head,tail)) { count = count - 1; }
	
	if(count > 0) {
	  unsigned int cost;
	  switch(edge_selection_heuristic) {
	  case MAXIMISE_DEGREE:
	    cost = graph.num_underlying_edges(head) + graph.num_underlying_edges(tail);
	    break;
	  case MAXIMISE_MDEGREE:
	    cost = graph.num_underlying_edges(head) * graph.num_underlying_edges(tail);
	    break;
	  case MINIMISE_DEGREE:
	    cost = 2*V - (graph.num_underlying_edges(head) + graph.num_underlying_edges(tail));
	    break;
	  case MINIMISE_SDEGREE:
	    cost = V - (std::min(graph.num_underlying_edges(head),graph.num_underlying_edges(tail)));
	    break;
	  case MINIMISE_MDEGREE:
	    cost = V*V - ((graph.num_underlying_edges(head) * graph.num_underlying_edges(tail)));
	    break;
	  case VERTEX_ORDER:
	    return typename G::edge_t(head,tail,count);
	    break;
	  case RANDOM:
	    if(rcount == rtarget) {
	      return typename G::edge_t(head,tail,count);
	    }
	    rcount += count;	    
	  }
	  if(cost > best) {
	    r = typename G::edge_t(head,tail,count);
	    best = cost;
	  }     
	} 
      }
    }
  }
  if(best == 0) { throw new std::runtime_error("internal failure"); }
  return r;
} 

/* deleteContract is the core algorithm for the tutte computation
 * it reduces a graph to two smaller graphs using a delete operation
 * for one, and a contract operation for the other.
 *
 * The algorithm also uses a number of tricks to prune the computation
 * space.  These include: eliminating small graphs using optimised, 
 * hand-coded decision procedures; storing previously seen graphs
 * in a cache; and, dynamically monitoring the "treeness" of the graph.
 */

template<class G, class P>
void deleteContract(G &graph, P &poly, unsigned int my_id) { 
  if(status_flag) { print_status(); }

  num_steps++;

  // first, eliminate any loops
  unsigned int num_loops = graph.remove_loops();  

  if(graph.is_tree()) {
    // termination for trees!
    if(xml_flag) { write_xml_leaf(my_id, graph); }
    poly += xy_term(graph.num_edges(),num_loops);
  } else if(graph.is_multi_tree()) {
    // termination for multi-graphs whose underlying
    // graph is a tree.
    P r(xy_term(0,num_loops));   

    if(xml_flag) { write_xml_leaf(my_id, graph); }    
    for(typename G::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
      for(typename G::edge_iterator j(graph.begin_edges(*i));
	  j!=graph.end_edges(*i);++j) {		
	unsigned int head = *i;
	unsigned int tail = j->first;
	unsigned int count = j->second;
	if(head < tail) { 
	  if(count > 1) {
	    P tmp(r);
	    tmp *= xy_term(1,0); // this could be optimised for sure!
	    r *= xy_term(0,1,count-1);
	    r += tmp;
	  } else {
	    r *= xy_term(1,0);
	  }
	}
      }
    }

    poly += r;
    } else {
    // Now, remove any pendant vertices (i.e. vertices of degree one).
    
    int num_pendants(0);

    while(graph.num_pendant_vertices() > 0) {
      int l = graph.select_pendant_vertex();
      graph.remove(l);
      num_pendants++;
    }

    xy_term xys(num_pendants,num_loops);    

    // Second, attempt to evaluate small graphs directly.  For big graphs,
    // look them up in the cache.
    unsigned char *key = NULL;
    if(graph.num_vertices() < small_graph_threshold) {      
      // if this is a small 
      /*
      if(!graph.is_multi_graph()) { 
	switch(graph.num_vertices()) {
	case 4:
	  return evaluate_simple_fours<Graph,Poly>(g) * ys * xs; 
	case 5:
	  return evaluate_simple_fives<Graph,Poly>(g) * ys * xs; 
	default:
	  break;
	}
      }
      */
    } else {     
      key = graph_key(graph);      
      if(cache.lookup(key,poly)) { 
	if(xml_flag) { write_xml_match(my_id,graph); }

	poly *= xys;
	delete [] key; // free space used by key
	return; 
      }                
    }
    
    // === XML OUTPUT STUFF ===
    
    unsigned int left_id = xml_id;
    unsigned int right_id = xml_id+1;
    xml_id = xml_id + 2; // allocate id's now so I know them!
    if(xml_flag) { write_xml_nonleaf(my_id,left_id,right_id,graph); }

    // === END ===

    // Third, perform delete contract 
    typename G::edge_t e = select_nontree_edge(graph);

    graph.remove_edge(e.first,e.second,e.third);        
    G g2(graph); 
    g2.contract_edge(e.first,e.second); 

    // Fourth, recursively compute the polynomial   
    P p2;

    deleteContract(graph, poly, left_id);
    deleteContract(g2,p2, right_id);

    if(e.third > 1) { p2 *= xy_term(0,0,e.third-1); }

    poly += p2;
    poly *= xys;

    // Finally, save computed polynomial
    if(key != NULL) {
      cache.store(key,poly);
      delete [] key;  // free space used by key
    }    
  }
}

// ---------------------------------------------------------------
// Input File Parser
// ---------------------------------------------------------------

int parse_number(unsigned int &pos, string const &str) {
  int s = pos;
  while(pos < str.length() && isdigit(str[pos])) {
    pos = pos + 1;
  }
  stringstream ss(str.substr(s,pos));
  int r;
  ss >> r;
  return r;
}

void match(char c, unsigned int &pos, string const &str) {
  if(pos >= str.length() || str[pos] != c) { throw runtime_error(string("syntax error -- expected '") + c + "', got '" + str[pos] + "'"); }
  ++pos;
}

template<class G>
G read_graph(std::istream &input) {
  vector<pair<unsigned int, unsigned int> > edgelist;
  unsigned int V = 0, pos = 0;
    
  bool firstTime=true;
  string in;
  input >> in;

  while(pos < in.length()) {
    if(!firstTime) { match(',',pos,in); }
    firstTime=false;
    // just keep on reading!
    unsigned int tail = parse_number(pos,in);
    match('-',pos,in); match('-',pos,in);
    unsigned int head = parse_number(pos,in);
    V = max(V,max(head,tail));
    edgelist.push_back(std::make_pair(tail,head));
  }  

  if(V == 0) { return G(0); }

  G r(V+1);

  for(vector<pair<unsigned int, unsigned int> >::iterator i(edgelist.begin());
      i!=edgelist.end();++i) {
    r.add_edge(i->first,i->second);
  }

  return r;
}

unsigned int parse_amount(char *str) {
  char *endp=NULL;
  long r = strtol(str,&endp,10);
  if(*endp != '\0') {
    if(strcmp(endp,"M") == 0) {
      r = r * 1024 * 1024;
    } else if(strcmp(endp,"K") == 0) {
      r = r * 1024;
    } else if(strcmp(endp,"G") == 0) {
      r = r * 1024 * 1024 * 1024;
    }
  }
  return r;
}

// ---------------------------------------------------------------
// Statistics Printing Methods
// ---------------------------------------------------------------

void write_bucket_lengths(fstream &out) {
  out << "############################" << endl;
  out << "# CACHE BUCKET LENGTH DATA #" << endl;
  out << "############################" << endl;
  out << "# Length\tCount" << endl;
  vector<int> counts;
  // first, count the lengths
  for(int i=0;i!=cache.num_buckets();++i) {
    int len = cache.bucket_length(i);
    if(counts.size() < (len+1)) {
      // need to increase size of count array
      counts.resize(len+1,0);
    }
    counts[len]++;
  }

  // second, print the data!
  for(unsigned int i=0;i!=counts.size();++i) {
    double percentage(((double)counts[i]*100) / cache.num_buckets());
    out << i << "\t" << counts[i] << "\t" << setprecision(2) << percentage << endl;
  }
}

void write_graph_sizes(fstream &out) {
  out << endl << endl;
  out << "#########################" << endl;
  out << "# CACHE GRAPH SIZE DATA #" << endl;
  out << "#########################" << endl;
  out << "# Size\t#Graphs (%)\t#MultiGraphs (%)" << endl;
  vector<int> counts;
  vector<int> mcounts;
  int nmgraphs=0;
  int ngraphs=0;
  // first, count the lengths
  for(simple_cache::iterator i(cache.begin());i!=cache.end();++i) {
    adjacency_list<> g(graph_from_key<adjacency_list<> >(i.key()));
    if(counts.size() < (g.num_vertices()+1)) {
      // need to increase size of count array
      counts.resize(g.num_vertices()+1,0);
    }
    ++ngraphs;
    counts[g.num_vertices()]++;
    if(g.is_multi_graph()) {
      nmgraphs++;
      if(mcounts.size() < (g.num_vertices()+1)) {
	// need to increase size of count array
	mcounts.resize(g.num_vertices()+1,0);
      }
      mcounts[g.num_vertices()]++;      
    }
  }

  // second, print the data!
  for(unsigned int i=0;i!=counts.size();++i) {
    double percentage(((double)counts[i]*100) / ngraphs);
    out << i << "\t" << counts[i] << "\t" << setprecision(2) << percentage;
    percentage = (((double)mcounts[i]*100) / nmgraphs);
    out << "\t" << mcounts[i] << "\t" << setprecision(2) << percentage << endl;
  }
}

void write_hit_counts(fstream &out) {
  std::set<pair<unsigned int, unsigned int> > hs;
  std::vector<triple<unsigned int, unsigned int, unsigned int> > table;
  out << endl << endl;
  out << "##############################" << endl;
  out << "# CACHE GRAPH HIT COUNT DATA #" << endl;
  out << "##############################" << endl;
  out << "# Hit Count\tV\tE\tE'" << endl;
  int nmgraphs=0;
  int ngraphs=0;

  for(simple_cache::iterator i(cache.begin());i!=cache.end();++i) {
    adjacency_list<> g(graph_from_key<adjacency_list<> >(i.key()));
    table.push_back(make_triple(g.num_vertices(),g.num_edges(),g.num_edges() - g.num_multiedges()));
    hs.insert(make_pair(i.hit_count(),table.size()-1));
  }
  
  for(std::set<pair<unsigned int, unsigned int> >::iterator i(hs.begin());
      i!=hs.end();++i) {
    triple<unsigned int, unsigned int, unsigned int> const &t(table[i->second]);
    out << i->first << "\t" << t.first << "\t" << t.second << "\t" << t.third << endl;
  }
}

// ---------------------------------------------------------------
// Signal Handlers
// ---------------------------------------------------------------

static int status_interval = 5; // in seconds

void timer_handler(int signum) {
  status_flag=true;
  alarm(status_interval);
}

void print_status() {
  status_flag=false;
  double rate = (num_steps - old_num_steps);
  double cf = (100*((double)cache.size())) / cache.capacity();
  rate /= status_interval;
  cout << "Completed " << num_steps << " graphs at rate of " << ((int) rate) << "/s, cache is " << setprecision(3) << cf << "% full." << endl;
  old_num_steps = num_steps;  
}

// ---------------------------------------------------------------
// Run Method
// ---------------------------------------------------------------

template<class G, class P>
void run(ifstream &input, unsigned int ngraphs, boolean quiet_mode) {
  unsigned int ngraphs_completed=0;
  if(xml_flag) { write_xml_start(); }
  while(!input.eof() && ngraphs_completed < ngraphs) {
    // first, reset all stats information
    cache.clear();  
    cache.reset_stats();
    num_steps = 0;

    // now, do stuff!
    G start_graph = read_graph<G>(input);
    unsigned int nedges(start_graph.num_edges());
    if(start_graph.num_vertices() == 0) { break; }
    if(xml_flag) {
      // do nout for now
    } else if(quiet_mode) {
      cout << start_graph.num_vertices() << "\t" << start_graph.num_edges();
    } else {
      cout << "VERTICES = " << start_graph.num_vertices() << ", EDGES = " << start_graph.num_edges() << endl << endl;
      print_graph(cout,start_graph);    
    }   
    
    my_timer timer;
    P tuttePoly;
    
    deleteContract<G,P>(start_graph,tuttePoly,1);        

    if(xml_flag) {
      // do nout for now.
    } else if(quiet_mode) {
      cout << "\t" << setprecision(3) << timer.elapsed() << "\t" << num_steps << "\t" << tuttePoly.substitute(1,1) << "\t" << tuttePoly.substitute(2,2) << endl;
    } else {
      cout << "Tutte Polynomial: " << tuttePoly.str() << endl << endl;
      
      cout << "T(1,1) = " << tuttePoly.substitute(1,1) << endl;
      cout << "T(2,2) = " << tuttePoly.substitute(2,2) << " (should be " << pow(biguint(2U),nedges) << ")" << endl;
      
      cout << "==================" << endl;
      cout << "Total Steps: " << num_steps << endl;
      cout << "Time : " << setprecision(3) << timer.elapsed() << "s" << endl;
      cout << endl;
      cout << "Cache stats:" << endl << "------------" << endl;
      cout << "Density: " << (cache.density()*1024*1024) << " graphs/MB" << endl;
      cout << "# Entries: " << cache.num_entries() << endl;
      cout << "# Cache Hits: " << cache.num_hits() << endl;
      cout << "# Cache Misses: " << cache.num_misses() << endl;
      cout << "# Cache Collisions: " << cache.num_collisions() << endl;
      cout << "Min Bucket Length: " << cache.min_bucket_size() << endl;
      cout << "Max Bucket Length: " << cache.max_bucket_size() << endl;
      // now, write out stats
      
      fstream stats_out("tutte-stats.dat",fstream::out);
      write_bucket_lengths(stats_out);
      write_graph_sizes(stats_out);
      write_hit_counts(stats_out);
    }
    ++ngraphs_completed;
  }
  if(xml_flag) { write_xml_end(); }
}

// ---------------------------------------------------------------
// Main Method
// ---------------------------------------------------------------

int main(int argc, char *argv[]) {

  // ------------------------------
  // Process command-line arguments
  // ------------------------------

  #define OPT_HELP 0
  #define OPT_QUIET 1  
  #define OPT_SMALLGRAPHS 5
  #define OPT_NGRAPHS 6
  #define OPT_CACHESIZE 10
  #define OPT_CACHEBUCKETS 11  
  #define OPT_CACHEREPLACEMENT 12
  #define OPT_CACHERANDOM 13
  #define OPT_NAUTYWORKSPACE 20
  #define OPT_SIMPLE_POLY 30
  #define OPT_FACTOR_POLY 31
  #define OPT_XML_OUT 32
  #define OPT_SMALL 40
  #define OPT_MEDIUM 41
  #define OPT_LARGE 42
  #define OPT_MAXDEGREE 50
  #define OPT_MAXMDEGREE 51
  #define OPT_MINDEGREE 52
  #define OPT_MINMDEGREE 53
  #define OPT_MINSDEGREE 54
  #define OPT_VERTEXORDER 55
  #define OPT_RANDOM 56
  #define OPT_BFSTREE 57
  #define OPT_DFSTREE 58

  struct option long_options[]={
    {"help",no_argument,NULL,OPT_HELP},
    {"cache-size",required_argument,NULL,OPT_CACHESIZE},
    {"cache-buckets",required_argument,NULL,OPT_CACHEBUCKETS},
    {"cache-replacement",required_argument,NULL,OPT_CACHEREPLACEMENT},
    {"cache-random-replacement",required_argument,NULL,OPT_CACHERANDOM},    
    {"minimise-degree", no_argument,NULL,OPT_MINDEGREE},
    {"minimise-mdegree", no_argument,NULL,OPT_MINMDEGREE},
    {"minimise-sdegree", no_argument,NULL,OPT_MINSDEGREE},
    {"maximise-degree", no_argument,NULL,OPT_MAXDEGREE},
    {"maximise-mdegree", no_argument,NULL,OPT_MAXMDEGREE},
    {"vertex-order", no_argument,NULL,OPT_VERTEXORDER},
    {"bfs-spanning-tree", no_argument, NULL, OPT_BFSTREE},
    {"dfs-spanning-tree", no_argument, NULL, OPT_DFSTREE},
    {"random", no_argument,NULL,OPT_RANDOM},
    {"nauty-workspace",required_argument,NULL,OPT_NAUTYWORKSPACE},
    {"small-graphs",required_argument,NULL,OPT_SMALLGRAPHS},
    {"simple-poly",no_argument,NULL,OPT_SIMPLE_POLY},
    {"xml-tree",no_argument,NULL,OPT_XML_OUT},
    {"ngraphs",required_argument,NULL,OPT_NGRAPHS},
    {"quiet",no_argument,NULL,OPT_QUIET},
    {"small",no_argument,NULL,OPT_SMALL},
    {"medium",no_argument,NULL,OPT_MEDIUM},
    {"large",no_argument,NULL,OPT_LARGE},
    NULL
  };
  
  char *descriptions[]={
    "        --help                    display this information",
    " -q     --quiet                   output stats summary as single line only (useful for generating data)",
    " -c     --cache-size=<amount>     set sizeof cache to allocate, e.g. 700M",
    "        --cache-buckets=<amount>  set number of buckets to use in cache, e.g. 10000",
    "        --nauty-workspace=<amount> set size of nauty workspace, e.g. 10000",
    "        --small-graphs=size        set threshold for small graphs, e.g. 7",
    "        --ngraphs=n               number of graphs to process from input file",
    NULL
  };

  unsigned int v;
  unsigned int cache_size(50*1024*1024); // detault 50M
  unsigned int cache_buckets(10000);     // default 10,000 buckets
  unsigned int poly_rep(OPT_FACTOR_POLY);
  unsigned int ngraphs(UINT_MAX);
  unsigned int size = OPT_LARGE;
  bool quiet_mode=false;
  bool bfs_tree = false;

  while((v=getopt_long(argc,argv,"qc:",long_options,NULL)) != -1) {
    switch(v) {      
    case OPT_HELP:
      cout << "usage: " << argv[0] << " [options] <input graph file>" << endl;
      cout << "options:" << endl;
      for(char **ptr=descriptions;*ptr != NULL; ptr++) {
	cout << *ptr << endl;
      }    
      exit(1);
          
    case 'q':
    case OPT_QUIET:      
      quiet_mode=true;
      break;
    case OPT_NGRAPHS:
      ngraphs = atoi(optarg);
      break;
    case OPT_XML_OUT:
      xml_flag=true;
      break;
    // --- CACHE OPTIONS ---
    case 'c':
    case OPT_CACHESIZE:
      cache_size = parse_amount(optarg);
      break;
    case OPT_CACHEBUCKETS:
      cache_buckets = parse_amount(optarg);
      break;
    case OPT_CACHEREPLACEMENT:
      cache.set_replacement(strtof(optarg,NULL));
      break;
    case OPT_CACHERANDOM:
      cache.set_random_replacement();
      break;
    // --- POLY OPTIONS ---
    case OPT_SIMPLE_POLY:
      poly_rep = OPT_SIMPLE_POLY;
      break;
    // --- HEURISTICS ---
    case OPT_MINDEGREE:
      edge_selection_heuristic = MINIMISE_DEGREE;
      break;
    case OPT_MAXDEGREE:
      edge_selection_heuristic = MAXIMISE_DEGREE;
      break;
    case OPT_MAXMDEGREE:
      edge_selection_heuristic = MAXIMISE_MDEGREE;
      break;
    case OPT_MINMDEGREE:
      edge_selection_heuristic = MINIMISE_MDEGREE;
      break;
    case OPT_MINSDEGREE:
      edge_selection_heuristic = MINIMISE_SDEGREE;
      break;
    case OPT_VERTEXORDER:
      edge_selection_heuristic = VERTEX_ORDER;
      break;
    case OPT_RANDOM:
      edge_selection_heuristic = RANDOM;
      break;
    case OPT_BFSTREE:
      bfs_tree = true;
      break;
    case OPT_DFSTREE:
      bfs_tree = false;
      break;
    // --- OTHER OPTIONS ---
    case OPT_NAUTYWORKSPACE:
      resize_nauty_workspace(parse_amount(optarg));
      break;
    case OPT_SMALLGRAPHS:
      small_graph_threshold = parse_amount(optarg);      
      break;      
    case OPT_SMALL:
    case OPT_MEDIUM:
    case OPT_LARGE:
      size=v;
      break;
    default:
      cout << "Unrecognised parameter!" << endl;
      exit(1);    
    }    
  }

  // Quick sanity check

  if(optind >= argc) {
    cout << "usage: " << argv[0] << " [options] <input graph file>" << endl;
    cout << "options:" << endl;
    for(char **ptr=descriptions;*ptr != NULL; ptr++) {
      cout << *ptr << endl;
    }    
    exit(1);
  }

  // -------------------------------------------------
  // Initialise Cache 
  // -------------------------------------------------

  cache.resize(cache_size);
  cache.rebucket(cache_buckets);

  // -------------------------------------------------
  // Register alarm signal for printing status updates
  // -------------------------------------------------

  if(!quiet_mode) {
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = &timer_handler;
    if(sigaction(SIGALRM,&sa,NULL)) { perror("sigvtalarm"); }
    alarm(status_interval); // trigger alarm in status_interval seconds
  }

  // -----------------------------------
  // Now, begin solving the input graph!
  // -----------------------------------

  srand(time(NULL));

  try {
    ifstream input(argv[optind]);    
    if(poly_rep == OPT_FACTOR_POLY) {
      if(size == OPT_SMALL) {
	if(bfs_tree) {
	  run<bfs_spanning_graph<adjacency_list<> >,factor_poly<safe<unsigned int> > >(input,ngraphs,quiet_mode);
	} else {
	  run<spanning_graph<adjacency_list<> >,factor_poly<safe<unsigned int> > >(input,ngraphs,quiet_mode);
	}
      } else if(size == OPT_MEDIUM) {
	if(bfs_tree) {
	  run<bfs_spanning_graph<adjacency_list<> >,factor_poly<safe<unsigned long long> > >(input,ngraphs,quiet_mode);
	} else {
	  run<spanning_graph<adjacency_list<> >,factor_poly<safe<unsigned long long> > >(input,ngraphs,quiet_mode);
	}
      } else {
	if(bfs_tree) {
	  run<bfs_spanning_graph<adjacency_list<> >,factor_poly<biguint> >(input,ngraphs,quiet_mode);
	} else {
	  run<spanning_graph<adjacency_list<> >,factor_poly<biguint> >(input,ngraphs,quiet_mode);
	}
      }
    } else {
      //      run<spanning_graph<adjacency_list<> >,simple_poly<> >(input,ngraphs,quiet_mode);
    }    
  } catch(bad_alloc const &e) {
    cout << "error: insufficient memory!" << endl;
  } catch(exception const &e) {
    cout << "error: " << e.what() << endl;
  }
}






                                            
