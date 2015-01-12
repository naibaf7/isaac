#include <set>
#include <string>

#include "atidlas/backend/mapped_object.h"
#include "atidlas/backend/parse.h"
#include "atidlas/backend/stream.h"
#include "atidlas/symbolic/expression.h"
#include "atidlas/tools/to_string.hpp"
#include "atidlas/tools/find_and_replace.hpp"

namespace atidlas
{


void mapped_object::postprocess(std::string &) const { }

void mapped_object::replace_offset(std::string & str, MorphBase const & morph)
{
  size_t pos = 0;
  while ((pos=str.find("$OFFSET", pos))!=std::string::npos)
  {
    std::string postprocessed;
    size_t pos_po = str.find('{', pos);
    size_t pos_pe = str.find('}', pos_po);

    if (MorphBase2D const * p = dynamic_cast<MorphBase2D const *>(&morph))
    {
      size_t pos_comma = str.find(',', pos_po);
      std::string i = str.substr(pos_po + 1, pos_comma - pos_po - 1);
      std::string j = str.substr(pos_comma + 1, pos_pe - pos_comma - 1);
      postprocessed = (*p)(i, j);
    }
    else if (MorphBase1D const * p = dynamic_cast<MorphBase1D const *>(&morph))
    {
      std::string i = str.substr(pos_po + 1, pos_pe - pos_po - 1);
      postprocessed = (*p)(i);
    }

    str.replace(pos, pos_pe + 1 - pos, postprocessed);
    pos = pos_pe;
  }
}

void mapped_object::register_attribute(std::string & attribute, std::string const & key, std::string const & value)
{
  attribute = value;
  keywords_[key] = attribute;
}

mapped_object::node_info::node_info(mapping_type const * _mapping, atidlas::symbolic_expression const * _symbolic_expression, int_t _root_idx) :
    mapping(_mapping), symbolic_expression(_symbolic_expression), root_idx(_root_idx) { }

mapped_object::mapped_object(std::string const & scalartype, unsigned int id, std::string const & type_key) : type_key_(type_key)
{
  register_attribute(scalartype_, "#scalartype", scalartype);
  register_attribute(name_, "#name", "obj" + tools::to_string(id));
}

mapped_object::~mapped_object()
{ }

std::string mapped_object::type_key() const
{ return type_key_; }

std::string const & mapped_object::name() const
{ return name_; }

std::map<std::string, std::string> const & mapped_object::keywords() const
{ return keywords_; }

std::string mapped_object::process(std::string const & in) const
{
  std::string res(in);
  for (std::map<std::string,std::string>::const_iterator it = keywords_.begin(); it != keywords_.end(); ++it)
    tools::find_and_replace(res, it->first, it->second);
  postprocess(res);
  return res;
}

std::string mapped_object::evaluate(std::map<std::string, std::string> const & accessors) const
{
  if (accessors.find(type_key_)==accessors.end())
    return name_;
  return process(accessors.at(type_key_));
}


binary_leaf::binary_leaf(mapped_object::node_info info) : info_(info){ }

void binary_leaf::process_recursive(kernel_generation_stream & stream, leaf_t leaf, std::multimap<std::string, std::string> const & accessors)
{
  std::set<std::string> already_fetched;
  process(stream, leaf, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping, already_fetched);
}

std::string binary_leaf::evaluate_recursive(leaf_t leaf, std::map<std::string, std::string> const & accessors)
{
  return evaluate(leaf, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}


mapped_mproduct::mapped_mproduct(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "mproduct"), binary_leaf(info) { }

//
mapped_reduction::mapped_reduction(std::string const & scalartype, unsigned int id, node_info info, std::string const & type_key) :
  mapped_object(scalartype, id, type_key), binary_leaf(info)
{ }

int_t mapped_reduction::root_idx() const
{ return info_.root_idx; }

atidlas::symbolic_expression const & mapped_reduction::symbolic_expression() const
{ return *info_.symbolic_expression; }

symbolic_expression_node mapped_reduction::root_node() const
{ return symbolic_expression().array()[root_idx()]; }

bool mapped_reduction::is_index_reduction() const
{
  op_element const & op = root_op();
  return op.type==OPERATOR_ELEMENT_ARGFMAX_TYPE
      || op.type==OPERATOR_ELEMENT_ARGMAX_TYPE
      || op.type==OPERATOR_ELEMENT_ARGFMIN_TYPE
      || op.type==OPERATOR_ELEMENT_ARGMIN_TYPE;
}

op_element mapped_reduction::root_op() const
{
    return info_.symbolic_expression->array()[info_.root_idx].op;
}


//
mapped_scalar_reduction::mapped_scalar_reduction(std::string const & scalartype, unsigned int id, node_info info) : mapped_reduction(scalartype, id, info, "scalar_reduction"){ }

//
mapped_mreduction::mapped_mreduction(std::string const & scalartype, unsigned int id, node_info info) : mapped_reduction(scalartype, id, info, "mreduction") { }

//
mapped_host_scalar::mapped_host_scalar(std::string const & scalartype, unsigned int id) : mapped_object(scalartype, id, "host_scalar"){ }

//
mapped_tuple::mapped_tuple(std::string const & scalartype, unsigned int id, size_t size) : mapped_object(scalartype, id, "tuple"+tools::to_string(size)), size_(size), names_(size)
{
  for(size_t i = 0 ; i < size_ ; ++i)
    register_attribute(names_[i], "#tuplearg"+tools::to_string(i), name_ + tools::to_string(i));
}

//
mapped_handle::mapped_handle(std::string const & scalartype, unsigned int id, std::string const & type_key) : mapped_object(scalartype, id, type_key)
{ register_attribute(pointer_, "#pointer", name_ + "_pointer"); }

//
mapped_scalar::mapped_scalar(std::string const & scalartype, unsigned int id) : mapped_handle(scalartype, id, "scalar") { }

//
mapped_buffer::mapped_buffer(std::string const & scalartype, unsigned int id, std::string const & type_key) : mapped_handle(scalartype, id, type_key){ }

//
void mapped_array::postprocess(std::string & str) const
{
  struct Morph : public MorphBase2D
  {
    Morph(std::string const & _ld, char _type) : ld(_ld), type(_type){ }
    std::string operator()(std::string const & i, std::string const & j) const
    {
      if(type=='c')
        return i;
      else if(type=='r')
        return j;
      else
        return "(" + i + ") +  (" + j + ") * " + ld;
    }
  private:
    std::string const & ld;
    char type;
  };
  replace_offset(str, Morph(ld_, type_));
}

mapped_array::mapped_array(std::string const & scalartype, unsigned int id, char type) : mapped_buffer(scalartype, id, "array"), type_(type)
{
  register_attribute(ld_, "#ld", name_ + "_ld");
  register_attribute(start1_, "#start1", name_ + "_start1");
  register_attribute(start2_, "#start2", name_ + "_start2");
  register_attribute(stride1_, "#stride1", name_ + "_stride1");
  register_attribute(stride2_, "#stride2", name_ + "_stride2");
  keywords_["#nldstride"] = "#stride2";
}

//
void mapped_vector_diag::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  tools::find_and_replace(res, "#diag_offset", atidlas::evaluate(RHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping));
  accessors["array"] = res;
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_vector_diag::mapped_vector_diag(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "vector_diag"), binary_leaf(info){ }

//
void mapped_trans::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  accessors["array"] = res;
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_trans::mapped_trans(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "matrix_trans"), binary_leaf(info){ }

//
void mapped_matrix_row::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  tools::find_and_replace(res, "#row", atidlas::evaluate(RHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping));
  accessors["array"] = res;
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_matrix_row::mapped_matrix_row(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "matrix_row"), binary_leaf(info)
{ }

//
void mapped_matrix_column::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  tools::find_and_replace(res, "#column", atidlas::evaluate(RHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping));
  accessors["array"] = res;
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_matrix_column::mapped_matrix_column(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "matrix_column"), binary_leaf(info)
{ }

//
void mapped_matrix_repeat::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  accessors["array"] = info_.mapping->at(std::make_pair(info_.root_idx,RHS_NODE_TYPE))->process(res);
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_matrix_repeat::mapped_matrix_repeat(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "matrix_repeat"), binary_leaf(info)
{ }


//
void mapped_matrix_diag::postprocess(std::string &res) const
{
  std::map<std::string, std::string> accessors;
  tools::find_and_replace(res, "#diag_offset", atidlas::evaluate(RHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping));
  accessors["array"] = res;
  res = atidlas::evaluate(LHS_NODE_TYPE, accessors, *info_.symbolic_expression, info_.root_idx, *info_.mapping);
}

mapped_matrix_diag::mapped_matrix_diag(std::string const & scalartype, unsigned int id, node_info info) : mapped_object(scalartype, id, "matrix_diag"), binary_leaf(info)
{ }



}