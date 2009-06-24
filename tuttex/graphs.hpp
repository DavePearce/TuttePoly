#ifndef GRAPHS_HPP
#define GRAPHS_HPP

#include <vector>

/* This method just compacts the vertex space used by the graph so
 * that it is numbered contiguously from 0.  This is done by
 * eliminating any vertices which have no edges.
 */
template<class G>
G compact_graph(G const &graph) {
  std::vector<unsigned int> labels(graph.num_vertices(),0);
  int counter = 0;

  for(typename G::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
    if(graph.num_edges(*i) > 0) {
      labels[*i] = counter++;
    }
  }
 
  // now, create new permuted graph
  G r(counter);
  
  for(typename G::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
    for(typename G::edge_iterator j(graph.begin_edges(*i));
	j!=graph.end_edges(*i);++j) {	      
      unsigned int head(*i);
      unsigned int tail(j->first);
      unsigned int count(j->second);
      if(head <= tail) {
	r.add_edge(labels[head],labels[tail],count);
      }
    }
  }
  
  return r;  
}

#endif