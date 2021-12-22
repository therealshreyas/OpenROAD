/*
 * Copyright (c) 2019, The Regents of the University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FR_MASTER_H_
#define _FR_MASTER_H_

#include <algorithm>

#include "db/obj/frBlockage.h"
#include "db/obj/frBoundary.h"
#include "db/obj/frMTerm.h"
#include "frBaseTypes.h"

namespace fr {
namespace io {
class Parser;
}
class frMaster : public frBlockObject
{
 public:
  // constructors
  frMaster(const frString& name)
      : frBlockObject(),
        name_(name),
        masterType_(dbMasterType::NONE){};
  // getters
  void getBBox(Rect& boxIn) const
  {
    if (boundaries_.size()) {
      boundaries_.begin()->getBBox(boxIn);
    }
    frCoord llx = boxIn.xMin();
    frCoord lly = boxIn.yMin();
    frCoord urx = boxIn.xMax();
    frCoord ury = boxIn.yMax();
    Rect tmpBox;
    for (auto& boundary : boundaries_) {
      boundary.getBBox(tmpBox);
      llx = llx < tmpBox.xMin() ? llx : tmpBox.xMin();
      lly = lly < tmpBox.yMin() ? lly : tmpBox.yMin();
      urx = urx > tmpBox.xMax() ? urx : tmpBox.xMax();
      ury = ury > tmpBox.yMax() ? ury : tmpBox.yMax();
    }
    for (auto& term : getTerms()) {
      for (auto& pin : term->getPins()) {
        for (auto& fig : pin->getFigs()) {
          fig->getBBox(tmpBox);
          llx = llx < tmpBox.xMin() ? llx : tmpBox.xMin();
          lly = lly < tmpBox.yMin() ? lly : tmpBox.yMin();
          urx = urx > tmpBox.xMax() ? urx : tmpBox.xMax();
          ury = ury > tmpBox.yMax() ? ury : tmpBox.yMax();
        }
      }
    }
    boxIn.init(llx, lly, urx, ury);
  }
  void getDieBox(Rect& boxIn) const { boxIn = dieBox_; }
  const std::vector<frBoundary>& getBoundaries() const { return boundaries_; }
  const std::vector<std::unique_ptr<frBlockage>>& getBlockages() const
  {
    return blockages_;
  }
  const frString& getName() const { return name_; }
  const std::vector<std::unique_ptr<frMTerm>>& getTerms() const
  {
    return terms_;
  }
  frMTerm* getTerm(const std::string& in) const
  {
    auto it = name2term_.find(in);
    if (it == name2term_.end()) {
      return nullptr;
    } else {
      return it->second;
    }
  }
  dbMasterType getMasterType() { return masterType_; }

  // setters
  void addTerm(std::unique_ptr<frMTerm> in)
  {
    in->setOrderId(terms_.size());
    in->setMaster(this);
    name2term_[in->getName()] = in.get();
    terms_.push_back(std::move(in));
  }
  const Rect& getDieBox() const { return dieBox_; }
  void setBoundaries(const std::vector<frBoundary> in)
  {
    boundaries_ = in;
    if (!boundaries_.empty()) {
      boundaries_.begin()->getBBox(dieBox_);
    }
    frCoord llx = dieBox_.xMin();
    frCoord lly = dieBox_.yMin();
    frCoord urx = dieBox_.xMax();
    frCoord ury = dieBox_.yMax();
    Rect tmpBox;
    for (auto& boundary : boundaries_) {
      boundary.getBBox(tmpBox);
      llx = std::min(llx, tmpBox.xMin());
      lly = std::min(lly, tmpBox.yMin());
      urx = std::max(urx, tmpBox.xMax());
      ury = std::max(ury, tmpBox.yMax());
    }
    dieBox_.init(llx, lly, urx, ury);
  }
  void setBlockages(std::vector<std::unique_ptr<frBlockage>>& in)
  {
    for (auto& blk : in) {
      blockages_.push_back(std::move(blk));
    }
  }
  void addBlockage(std::unique_ptr<frBlockage> in)
  {
    blockages_.push_back(std::move(in));
  }
  void setMasterType(const dbMasterType& in) { masterType_ = in; }
  // others
  frBlockObjectEnum typeId() const override { return frcMaster; }

 protected:
  frString name_;

  dbMasterType masterType_;

  std::map<std::string, frMTerm*> name2term_;
  std::vector<std::unique_ptr<frMTerm>> terms_;

  std::vector<std::unique_ptr<frBlockage>> blockages_;

  std::vector<frBoundary> boundaries_;

  Rect dieBox_;

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version);

  frMaster() = default;  // for serialization

  friend class boost::serialization::access;
  friend class io::Parser;
};

template <class Archive>
void frMaster::serialize(Archive& ar, const unsigned int version)
{
  (ar) & boost::serialization::base_object<frBlockObject>(*this);
  (ar) & name_;
  (ar) & name2term_;
  (ar) & terms_;
  (ar) & blockages_;
  (ar) & boundaries_;
}
}  // namespace fr

#endif
