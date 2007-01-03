#ifndef NAUTYGRAPH_HPP
#define NAUTYGRAPH_HPP

#define MAXN 0
#include "nauty.h" // nauty include must come first, otherwise it interferes with std::set
#include <iostream>
#include <cstring>
#include <stdexcept>

// this is a problem
static graph *workspace = NULL;
static int worksize = 0;

class nauty_graph {
private:
  int N;
  int M;      // MAXM, cached for efficiency
  int E;
  graph *ptr; // pointer to the data
  
public:
  nauty_graph(int N) {
    M = N / WORDSIZE;
    if((N % WORDSIZE) > 0) { M++; }
    ptr = new setword[N*M];
    E = 0;
  }

  // copy constructor
  nauty_graph(nauty_graph const &g) {
    N = g.N;
    M = g.M;
    E = g.E;
    ptr = new setword[N*M];    
    memcpy(ptr,g.ptr,M*N*sizeof(setword));
  }

  ~nauty_graph() { delete [] ptr; }
  
  template<class T>
  nauty_graph(T g) {
    N = g.num_vertices();
    M = N / WORDSIZE;
    E = 0;
    if((N % WORDSIZE) > 0) { M++; }    
    ptr = new setword[N*M];
    for(int i=0;i!=N*M;++i) { ptr[i] = 0; } // this is wierd.  I thought this should be initialised to 0 by default?
    graph *p = ptr;

    // now create the graph
    for(typename T::vertex_iterator i(g.begin_verts());i!=g.end_verts();++i) {
      int v = *i;
      for(typename T::edge_iterator j(g.begin_edges(v));j!=g.end_edges(v);++j) {
	int w = *j;
	E ++;
	// represents edge v--w
	unsigned int wb = w / WORDSIZE;      // index of word holding succ bit for w
	unsigned int wo = w - (wb*WORDSIZE); // offset within word for succ bit
	p[wb] |= (1U << (WORDSIZE-wo));      // set succ bit!
      }
      p += M;
    }
    E = E >> 1; // since every edge is added twice!
  }

  nauty_graph const &operator=(nauty_graph const &g) {
    if(this != &g) { 
      N = g.N;
      M = g.M;
      E = g.E;
      ptr = new setword[N*M];    
      memcpy(ptr,g.ptr,M*N*sizeof(setword));
    }
    return *this;
  }
  
  // missing equality operator
  bool operator==(nauty_graph const &g) const {
    if(N != g.N || E != g.E) { return false; }
    else {
      for(int i=0;i!=N*M;++i) {
	if(ptr[i] != g.ptr[i]) { return false; }
      }
    }
    return true;
  }

  // hash operator
  size_t hash() const {
    int e = N*M;
    size_t r = 0;
    for(int i=0;i!=e;++i) { r ^= ptr[i]; }
    return r;
  }

  void makeCanonical() {
    if(worksize < (50*M)) {
      delete [] workspace;
      workspace = new setword[50*M];
      worksize = 50*M; // could change this parameter ??
    }
    
    graph *optr = ptr;
    ptr = new setword[N*M];
    statsblk stats;

    // options
    DEFAULTOPTIONS(opts); // could make static to save space   
    opts.getcanon=TRUE;
    opts.defaultptn = FALSE;
    opts.writemarkers = FALSE;

    int lab[N];
    int ptn[N];    
    for(int i=0;i!=N;++i) { 
      lab[i] = i; 
      ptn[i] = 1;
    }
    ptn[N-1] = 0;
    nvector orbits[N];
    // call nauty
    nauty(optr,lab,ptn,NULL,orbits,&opts,&stats,workspace,worksize,M,N,ptr);

    // tidy up
    delete [] optr;

    // check for error
    if(stats.errstatus != 0) {
      throw std::runtime_error("internal error: nauty returned an error?");
    }
  }

  void print() {
    setword *p = ptr; 

    std::cout << "V = { 0.." << N << " }" << std::endl;
    std::cout << "E = { ";

    for(int i=0;i!=N;++i) {
      int bp=0;
      for(int j=0;j!=M;++j,bp=bp+WORDSIZE,p=p+1) {
	setword mask = 1U;
	// could eliminate first check in loop condition
	// by splitting out the last iteration.
	for(int k=0;k!=WORDSIZE;++k) {
	  if(((*p) & mask)) { 
	    int tail = (j*WORDSIZE) + (WORDSIZE-k);
	    if(i <= tail) {
	      std::cout << i << "--" << tail << " "; 
	    }
	  }
	  mask = mask << 1;
	}
      }
    }
    std::cout << " }" << std::endl;
  }
  
  template<class T>
  T toGraph() {
    T g(N);
    graph *p = ptr;

    // now build the graph
    for(int i=0;i!=N;++i) {
      int bp=0;
      for(int j=0;j!=M;++j,bp=bp+WORDSIZE,p=p+1) {
	setword mask = 1U;
	// could eliminate first check in loop condition
	// by splitting out the last iteration.
	for(int k=0;k!=WORDSIZE;++k) {
	  if((*p & mask) != 0) { g.add_edge(i,bp+ (WORDSIZE-k)); }
	  mask = mask << 1;
	}
      }    
    }
    
    return g;
  }
};

// standard hash function for nauty graph
class hash_nauty_graph {
public:
  size_t operator()(nauty_graph const &g) const {
    return g.hash();
  }
};

#endif