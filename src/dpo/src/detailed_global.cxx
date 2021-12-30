///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2021, Andrew Kennings
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes.
////////////////////////////////////////////////////////////////////////////////
#include "detailed_global.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <cmath>
#include <iostream>
#include <stack>
#include <utility>
#include "utl/Logger.h"
#include "detailed_hpwl.h"
#include "detailed_manager.h"
#include "detailed_orient.h"
#include "detailed_segment.h"
#include "rectangle.h"
#include "utility.h"
#ifdef USE_OPENMP
#include <parallel/algorithm>
#include "omp.h"
#endif

using utl::DPO;

namespace dpo {

////////////////////////////////////////////////////////////////////////////////
// Defines.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Classes.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap(Architecture* arch, Network* network,
                                       RoutingParams* rt)
    : DetailedGenerator(),
      m_mgr(0),
      m_arch(arch),
      m_network(network),
      m_rt(rt),
      m_skipNetsLargerThanThis(100) {
  m_name = "global swap";
  m_attempts = 0;
  m_moves = 0;
  m_swaps = 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap()
    : DetailedGenerator(),
      m_mgr(0),
      m_arch(0),
      m_network(0),
      m_rt(0),
      m_skipNetsLargerThanThis(100) {
  m_name = "global swap";
  m_attempts = 0;
  m_moves = 0;
  m_swaps = 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::~DetailedGlobalSwap() {}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr, std::string command) {
  // A temporary interface to allow for a string which we will decode to create
  // the arguments.
  std::string scriptString = command;
  boost::char_separator<char> separators(" \r\t\n;");
  boost::tokenizer<boost::char_separator<char> > tokens(scriptString,
                                                        separators);
  std::vector<std::string> args;
  for (boost::tokenizer<boost::char_separator<char> >::iterator it =
           tokens.begin();
       it != tokens.end(); it++) {
    args.push_back(*it);
  }
  run(mgrPtr, args);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr,
                             std::vector<std::string>& args) {
  // Given the arguments, figure out which routine to run to do the reordering.

  m_mgr = mgrPtr;
  m_arch = m_mgr->getArchitecture();
  m_network = m_mgr->getNetwork();
  m_rt = m_mgr->getRoutingParams();

  int passes = 1;
  double tol = 0.01;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    }
  }
  passes = std::max(passes, 1);
  tol = std::max(tol, 0.01);

  double last_hpwl, curr_hpwl, init_hpwl, hpwl_x, hpwl_y;

  curr_hpwl = Utility::hpwl(m_network, hpwl_x, hpwl_y);
  init_hpwl = curr_hpwl;
  for (int p = 1; p <= passes; p++) {
    last_hpwl = curr_hpwl;

    // XXX: Actually, global swapping is nothing more than random
    // greedy improvement in which the move generating is done
    // using this object to generate a target which is the optimal
    // region for each candidate cell.
    globalSwap();

    curr_hpwl = Utility::hpwl(m_network, hpwl_x, hpwl_y);

    m_mgr->getLogger()->info(
        DPO, 306, "Pass {:3d} of global swaps; hpwl is {:.6e}.", p,
        curr_hpwl);

    if (std::fabs(curr_hpwl - last_hpwl) / last_hpwl <= tol) {
      break;
    }
  }
  double curr_imp = (((init_hpwl - curr_hpwl) / init_hpwl) * 100.);
  m_mgr->getLogger()->info(
      DPO, 307,
      "End of global swaps; objective is {:.6e}, improvement is {:.2f} percent.",
      curr_hpwl, curr_imp);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::globalSwap() {
  // Nothing for than random greedy improvement with only a hpwl objective
  // and done such that every candidate cell is considered once!!!

  m_traversal = 0;
  m_edgeMask.resize(m_network->getNumEdges());
  std::fill(m_edgeMask.begin(), m_edgeMask.end(), 0);

  m_mgr->resortSegments();

  // Get candidate cells.
  std::vector<Node*> candidates = m_mgr->m_singleHeightCells;
  Utility::random_shuffle(candidates.begin(), candidates.end(), m_mgr->m_rng);

  // Wirelength objective.
  DetailedHPWL hpwlObj(m_arch, m_network, m_rt);
  hpwlObj.init(m_mgr, NULL);  // Ignore orientation.

  double currHpwl = hpwlObj.curr();
  double nextHpwl = 0.;
  // Consider each candidate cell once.
  for (int attempt = 0; attempt < candidates.size(); attempt++) {
    Node* ndi = candidates[attempt];

    if (generate(ndi) == false) {
      continue;
    }

    double delta = hpwlObj.delta(m_mgr->m_nMoved, m_mgr->m_movedNodes,
                                 m_mgr->m_curX, m_mgr->m_curY, m_mgr->m_curOri,
                                 m_mgr->m_newX, m_mgr->m_newY, m_mgr->m_newOri);

    nextHpwl = currHpwl - delta;  // -delta is +ve is less.

    if (nextHpwl <= currHpwl) {
      m_mgr->acceptMove();

      currHpwl = nextHpwl;
    } else {
      m_mgr->rejectMove();
    }
  }
  return;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::getRange(Node* nd, Rectangle& nodeBbox) {
  // Determines the median location for a node.

  Edge* ed;
  unsigned mid;
  unsigned n;

  Pin* pin;
  unsigned t = 0;

  m_xpts.erase(m_xpts.begin(), m_xpts.end());
  m_ypts.erase(m_ypts.begin(), m_ypts.end());
  for (n = 0; n < nd->getPins().size(); n++) {
    pin = nd->getPins()[n];

    ed = pin->getEdge();

    nodeBbox.reset();

    //int numPins = ed->getNumPins();
    int numPins = ed->getPins().size();
    if (numPins <= 1) {
      continue;
    } else if (numPins > m_skipNetsLargerThanThis) {
      continue;
    } else {
      if (!calculateEdgeBB(ed, nd, nodeBbox)) {
        continue;
      }
    }

    // We've computed an interval for the pin.  We need to alter it to work for
    // the cell center. Also, we need to avoid going off the edge of the chip.
    nodeBbox.set_xmin(
        std::min(std::max(m_arch->getMinX(), nodeBbox.xmin() - pin->getOffsetX()),
                 m_arch->getMaxX()));
    nodeBbox.set_xmax(
        std::max(std::min(m_arch->getMaxX(), nodeBbox.xmax() - pin->getOffsetX()),
                 m_arch->getMinX()));
    nodeBbox.set_ymin(
        std::min(std::max(m_arch->getMinY(), nodeBbox.ymin() - pin->getOffsetY()),
                 m_arch->getMaxY()));
    nodeBbox.set_ymax(
        std::max(std::min(m_arch->getMaxY(), nodeBbox.ymax() - pin->getOffsetY()),
                 m_arch->getMinY()));

    // Record the location and pin offset used to generate this point.

    m_xpts.push_back(nodeBbox.xmin());
    m_xpts.push_back(nodeBbox.xmax());

    m_ypts.push_back(nodeBbox.ymin());
    m_ypts.push_back(nodeBbox.ymax());

    ++t;
    ++t;
  }

  // If, for some weird reason, we didn't find anything connected, then
  // return false to indicate that there's nowhere to move the cell.
  if (t <= 1) {
    return false;
  }

  // Get the median values.
  mid = t / 2;

  std::sort(m_xpts.begin(), m_xpts.end());
  std::sort(m_ypts.begin(), m_ypts.end());

  nodeBbox.set_xmin(m_xpts[mid - 1]);
  nodeBbox.set_xmax(m_xpts[mid]);

  nodeBbox.set_ymin(m_ypts[mid - 1]);
  nodeBbox.set_ymax(m_ypts[mid]);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::calculateEdgeBB(Edge* ed, Node* nd, Rectangle& bbox) {
  // Computes the bounding box of an edge.  Node 'nd' is the node to SKIP.
  double curX, curY;

  bbox.reset();

  int count = 0;
  for (int pe = 0; pe < ed->getPins().size(); pe++) {
    Pin* pin = ed->getPins()[pe];

    Node* other = pin->getNode();
    if (other == nd) {
      continue;
    }
    curX = other->getX() + pin->getOffsetX();
    curY = other->getY() + pin->getOffsetY();

    bbox.set_xmin(std::min(curX, bbox.xmin()));
    bbox.set_xmax(std::max(curX, bbox.xmax()));
    bbox.set_ymin(std::min(curY, bbox.ymin()));
    bbox.set_ymax(std::max(curY, bbox.ymax()));

    ++count;
  }

  return (count == 0) ? false : true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedGlobalSwap::delta(Node* ndi, double new_x, double new_y) {
  // Compute change in wire length for moving node to new position.

  double old_wl = 0.;
  double new_wl = 0.;
  double x, y;
  double old_xmin, old_xmax, old_ymin, old_ymax;
  double new_xmin, new_xmax, new_ymin, new_ymax;

  ++m_traversal;
  for (int pi = 0; pi < ndi->getPins().size(); pi++) {
    Pin* pini = ndi->getPins()[pi];

    Edge* edi = pini->getEdge();

    //int npins = edi->getNumPins();
    int npins = edi->getPins().size();
    if (npins <= 1 || npins >= m_skipNetsLargerThanThis) {
      continue;
    }
    if (m_edgeMask[edi->getId()] == m_traversal) {
      continue;
    }
    m_edgeMask[edi->getId()] = m_traversal;

    old_xmin = std::numeric_limits<double>::max();
    old_xmax = -std::numeric_limits<double>::max();
    old_ymin = std::numeric_limits<double>::max();
    old_ymax = -std::numeric_limits<double>::max();

    new_xmin = std::numeric_limits<double>::max();
    new_xmax = -std::numeric_limits<double>::max();
    new_ymin = std::numeric_limits<double>::max();
    new_ymax = -std::numeric_limits<double>::max();

    for (int pj = 0; pj < edi->getPins().size(); pj++) {
      Pin* pinj = edi->getPins()[pj];

      Node* ndj = pinj->getNode();

      x = ndj->getX() + pinj->getOffsetX();
      y = ndj->getY() + pinj->getOffsetY();

      old_xmin = std::min(old_xmin, x);
      old_xmax = std::max(old_xmax, x);
      old_ymin = std::min(old_ymin, y);
      old_ymax = std::max(old_ymax, y);

      if (ndj == ndi) {
        x = new_x + pinj->getOffsetX();
        y = new_y + pinj->getOffsetY();
      }
      new_xmin = std::min(new_xmin, x);
      new_xmax = std::max(new_xmax, x);
      new_ymin = std::min(new_ymin, y);
      new_ymax = std::max(new_ymax, y);
    }
    old_wl += old_xmax - old_xmin + old_ymax - old_ymin;
    new_wl += new_xmax - new_xmin + new_ymax - new_ymin;
  }
  return old_wl - new_wl;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedGlobalSwap::delta(Node* ndi, Node* ndj) {
  // Compute change in wire length for swapping the two nodes.

  double old_wl = 0.;
  double new_wl = 0.;
  double x, y;
  double old_xmin, old_xmax, old_ymin, old_ymax;
  double new_xmin, new_xmax, new_ymin, new_ymax;
  Node* nodes[2];
  nodes[0] = ndi;
  nodes[1] = ndj;

  ++m_traversal;
  for (int c = 0; c <= 1; c++) {
    Node* ndi = nodes[c];
    for (int pi = 0; pi < ndi->getPins().size(); pi++) {
      Pin* pini = ndi->getPins()[pi];

      Edge* edi = pini->getEdge();

      int npins = edi->getPins().size();
      if (npins <= 1 || npins >= m_skipNetsLargerThanThis) {
        continue;
      }
      if (m_edgeMask[edi->getId()] == m_traversal) {
        continue;
      }
      m_edgeMask[edi->getId()] = m_traversal;

      old_xmin = std::numeric_limits<double>::max();
      old_xmax = -std::numeric_limits<double>::max();
      old_ymin = std::numeric_limits<double>::max();
      old_ymax = -std::numeric_limits<double>::max();

      new_xmin = std::numeric_limits<double>::max();
      new_xmax = -std::numeric_limits<double>::max();
      new_ymin = std::numeric_limits<double>::max();
      new_ymax = -std::numeric_limits<double>::max();

      for (int pj = 0; pj < edi->getPins().size(); pj++) {
        Pin* pinj = edi->getPins()[pj];

        Node* ndj = pinj->getNode();

        x = ndj->getX() + pinj->getOffsetX();
        y = ndj->getY() + pinj->getOffsetY();

        old_xmin = std::min(old_xmin, x);
        old_xmax = std::max(old_xmax, x);
        old_ymin = std::min(old_ymin, y);
        old_ymax = std::max(old_ymax, y);

        if (ndj == nodes[0]) {
          ndj = nodes[1];
        } else if (ndj == nodes[1]) {
          ndj = nodes[0];
        }

        x = ndj->getX() + pinj->getOffsetX();
        y = ndj->getY() + pinj->getOffsetY();

        new_xmin = std::min(new_xmin, x);
        new_xmax = std::max(new_xmax, x);
        new_ymin = std::min(new_ymin, y);
        new_ymax = std::max(new_ymax, y);
      }

      old_wl += old_xmax - old_xmin + old_ymax - old_ymin;
      new_wl += new_xmax - new_xmin + new_ymax - new_ymin;
    }
  }
  return old_wl - new_wl;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(Node* ndi) {
  double xi = ndi->getX();
  double yi = ndi->getY();

  // Determine optimal region.
  Rectangle bbox;
  if (!getRange(ndi, bbox)) {
    return false;
  }
  // If cell inside box, do nothing.
  if (xi >= bbox.xmin() && xi <= bbox.xmax() && yi >= bbox.ymin() &&
      yi <= bbox.ymax()) {
    return false;
  }

  if (m_mgr->m_reverseCellToSegs[ndi->getId()].size() != 1) {
    return false;
  }
  int si = m_mgr->m_reverseCellToSegs[ndi->getId()][0]->getSegId();

  // We can move the cell to anywhere within the optimal box so what
  // should we do?  I think right now I will simply try to move or
  // swap it with something near the center of its box.  With regions,
  // this might not work too well if the box is outside of the region.
  //
  // Another choice would be to try a few times with random points
  // within the optimal region.  Consider this in the future...
  {
    double xj = 0.5 * (bbox.xmin() + bbox.xmax());
    double yj = 0.5 * (bbox.ymin() + bbox.ymax());

    // Row and segment for the destination.
    int rj = m_arch->find_closest_row(yj - 0.5 * ndi->getHeight());
    yj = m_arch->getRow(rj)->getBottom() + 0.5 * ndi->getHeight();
    int sj = -1;
    for (int s = 0; s < m_mgr->m_segsInRow[rj].size(); s++) {
      DetailedSeg* segPtr = m_mgr->m_segsInRow[rj][s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }
    if (sj == -1) {
      return false;
    }
    if (ndi->getRegionId() != m_mgr->m_segments[sj]->getRegId()) {
      return false;
    }

    bool isMoveOkay = false;
    if (!isMoveOkay) {
      if (si != sj) {
        isMoveOkay |= m_mgr->tryMove1(ndi, xi, yi, si, xj, yj, sj);
      } else {
        isMoveOkay |= m_mgr->tryMove2(ndi, xi, yi, si, xj, yj, sj);
      }
    }
    if (isMoveOkay) {
      ++m_moves;
      return true;
    }

    bool isSwapOkay = false;
    if (!isSwapOkay) {
      isSwapOkay |= m_mgr->trySwap1(ndi, xi, yi, si, xj, yj, sj);
    }
    if (isSwapOkay) {
      ++m_swaps;
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::init(DetailedMgr* mgr) {
  m_mgr = mgr;
  m_arch = mgr->getArchitecture();
  m_network = mgr->getNetwork();
  m_rt = mgr->getRoutingParams();

  m_traversal = 0;
  m_edgeMask.resize(m_network->getNumEdges());
  std::fill(m_edgeMask.begin(), m_edgeMask.end(), 0);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(DetailedMgr* mgr,
                                  std::vector<Node*>& candidates) {
  ++m_attempts;

  m_mgr = mgr;
  m_arch = mgr->getArchitecture();
  m_network = mgr->getNetwork();
  m_rt = mgr->getRoutingParams();

  Node* ndi = candidates[(*(m_mgr->m_rng))() % (candidates.size())];

  return generate(ndi);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::stats() {
  m_mgr->getLogger()->info( DPO, 334, "Generator {:s}, "
    "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
    getName().c_str(), m_attempts, m_swaps, m_moves );
}

}  // namespace dpo
