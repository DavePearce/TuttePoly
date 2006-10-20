#ifndef GRAPH_ALGORITHMS_HPP
#define GRAPH_ALGORITHMS_HPP

#include <iostream>

template<class T>
void print_graph(ostream &ostr, T const &graph) {
  ostr << "V = { ";
  for(typename T::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
    ostr << *i << " ";
  }
  ostr << "}" << endl;

  ostr << "E = { ";
  for(typename T::vertex_iterator i(graph.begin_verts());i!=graph.end_verts();++i) {
    for(typename T::edge_iterator j(graph.begin_edges(*i));j!=graph.end_edges(*i);++j) {
      ostr << *i << "--" << *j << " ";
    }
  }
  ostr << "}" << endl;
}

#endif
