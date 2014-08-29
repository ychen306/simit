#include "ir.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <string.h>

#include "types.h"
#include "util.h"

using namespace std;

namespace simit {
namespace internal {

// class IRNode
IRNode::~IRNode() {}

std::ostream &operator<<(std::ostream &os, const IRNode &node){
  node.print(os);
  return os;
}


// class TensorNode
TensorNode::~TensorNode() {
  delete type;
}

void TensorNode::print(std::ostream &os) const {
  os << getName() << " : " << *type;
}


// class Literal
Literal::Literal(TensorType *type) : TensorNode(type) {
  int componentSize = TensorType::componentSize(type->getComponentType());
  this->dataSize = type->getSize() * componentSize;
  this->data = malloc(dataSize);
}

Literal::Literal(TensorType *type, void *values) : Literal(type) {
  memcpy(this->data, values, this->dataSize);
}

Literal::~Literal() {
  free(data);
}

void Literal::clear() {
  memset(data, 0, dataSize);
}

void Literal::cast(TensorType *type) {
  assert(this->getType()->getComponentType() == type->getComponentType() &&
         this->getType()->getSize() == getType()->getSize());
  setType(type);
}

void Literal::print(std::ostream &os) const {
  for (auto &dim : getType()->getDimensions()) {
    assert(dim.getFactors().size() == 1 && "literals can't be hierarchical");
  }

  // TODO: Fix value printing to print matrices and tensors properly
  switch (getType()->getComponentType()) {
    case Type::INT: {
      int *idata = (int*)data;
      if (getType()->getSize() == 1) {
        os << idata[0];
      }
      else {
        os << "[" << idata[0];
        for (int i=0; i < getType()->getSize(); ++i) {
          os << ", " << idata[i];
        }
        os << "]";
      }
      break;
    }
    case Type::FLOAT: {
      double *fdata = (double*)data;
      if (getType()->getSize() == 1) {
        os << fdata[0];
      }
      else {
        os << "[" << to_string(fdata[0]);
        for (int i=1; i < getType()->getSize(); ++i) {
          os << ", " + to_string(fdata[i]);
        }
        os << "]";
      }
      break;
    }
    case Type::ELEMENT:
      assert(false && "Unsupported (TODO)");
      break;
    default:
      UNREACHABLE;
  }

  os << " : " << *getType();
}

bool operator==(const Literal& l, const Literal& r) {
  if (*l.getType() != *r.getType()) {
    return false;
  }
  assert(l.getType()->getSize() == r.getType()->getSize());
  simit::Type ctype = l.getType()->getComponentType();
  int byteSize = l.getType()->getSize() * TensorType::componentSize(ctype);

  if (memcmp(l.getData(), r.getData(), byteSize) != 0) {
    return false;
  }
  return true;
}

bool operator!=(const Literal& l, const Literal& r) {
  return !(l == r);
}


// class IndexVar
std::string IndexVar::operatorString(Operator op) {
  switch (op) {
    case IndexVar::FREE:
      return "free";
    case IndexVar::SUM:
      return "sum";
    case IndexVar::PRODUCT:
      return "product";
    default:
      UNREACHABLE;
  }
}

std::string IndexVar::operatorSymbol(Operator op) {
  switch (op) {
    case IndexVar::FREE:
      return "";
    case IndexVar::SUM:
      return "+";
    case IndexVar::PRODUCT:
      return "*";
    default:
      UNREACHABLE;
  }
}

std::ostream &operator<<(std::ostream &os, const IndexVar &var) {
  return os << IndexVar::operatorSymbol(var.getOperator()) << var.getName();
}


// class IndexedTensor
typedef std::vector<std::shared_ptr<IndexVar>> IndexVariables;
IndexedTensor::IndexedTensor(const std::shared_ptr<TensorNode> &tensor,
                             const IndexVariables &indexVars){
  assert(indexVars.size() == tensor->getOrder());
  for (size_t i=0; i < indexVars.size(); ++i) {
    assert(indexVars[i]->getIndexSet() == tensor->getType()->getDimensions()[i]
           && "IndexVar domain does not match tensordimension");
  }

  this->tensor = tensor;
  this->indexVariables = indexVars;
}

std::ostream &operator<<(std::ostream &os, const IndexedTensor &t) {
  os << t.getTensor()->getName() << "(";
  auto it = t.getIndexVariables().begin();
  if (it != t.getIndexVariables().end()) {
    os << (*it)->getName();
    ++it;
  }
  while (it != t.getIndexVariables().end()) {
    os << "," << (*it)->getName();
    ++it;
  }
  os << ")";
  return os;
}


// class IndexExpr
int IndexExpr::numOperands(Operator op) {
  return (op == NONE || op == NEG) ? 1 : 2;
}

IndexExpr::IndexExpr(const std::vector<std::shared_ptr<IndexVar>> &indexVars,
                     Operator op, const std::vector<IndexedTensor> &operands)
    : TensorNode(NULL), indexVars(indexVars), op(op), operands(operands) {
  initType();

  // Can't have reduction variables on rhs
  for (auto &idxVar : indexVars) {
    assert(idxVar->getOperator() == IndexVar::Operator::FREE);
  }

  // Operand typechecks
  assert(operands.size() == (size_t)IndexExpr::numOperands(op));
  Type firstType = operands[0].getTensor()->getType()->getComponentType();
  for (auto &operand : operands) {
    assert(firstType == operand.getTensor()->getType()->getComponentType() &&
           "Operand component types differ");
  }
}

IndexExpr::IndexExpr(IndexExpr::Operator op,
                     const vector<IndexedTensor> &operands)
    : IndexExpr(std::vector<std::shared_ptr<IndexVar>>(), op, operands) {

}

void IndexExpr::setIndexVariables(const vector<shared_ptr<IndexVar>> &ivs) {
  this->indexVars = ivs;
  initType();
}

void IndexExpr::setOperator(IndexExpr::Operator op) {
  this->op = op;
}

void IndexExpr::setOperands(const std::vector<IndexedTensor> &operands) {
  assert(operands.size() > 0);
  bool reinit = (operands[0].getTensor()->getType()->getComponentType() !=
                 this->operands[0].getTensor()->getType()->getComponentType());
  this->operands = operands;
  if (reinit) {
    initType();
  }
}

vector<shared_ptr<IndexVar>> IndexExpr::getDomain() const {
  vector<shared_ptr<IndexVar>> domain;
  set<shared_ptr<IndexVar>> added;
  for (auto &iv : indexVars) {
    if (added.find(iv) == added.end()) {
      added.insert(iv);
      domain.push_back(iv);
    }
  }
  for (auto &operand : operands) {
    for (auto &iv : operand.getIndexVariables()) {
      if (added.find(iv) == added.end()) {
        assert(iv->getOperator() != IndexVar::FREE
               && "freevars not used on lhs");
        added.insert(iv);
        domain.push_back(iv);
      }
    }
  }
  return domain;

}

static std::string opString(IndexExpr::Operator op) {
  std::string opstr;
  switch (op) {
    case IndexExpr::Operator::NONE:
      return "";
    case IndexExpr::Operator::NEG:
      return "-";
    case IndexExpr::Operator::ADD:
      return "+";
    case IndexExpr::Operator::SUB:
      return "-";
    case IndexExpr::Operator::MUL:
      return "*";
    case IndexExpr::Operator::DIV:
      return "//";
    default:
      UNREACHABLE;
  }
}

void IndexExpr::print(std::ostream &os) const {
  std::string idxVarStr =
      (indexVars.size()!=0) ? "(" + simit::util::join(indexVars,",") + ")" : "";
  os << getName() << idxVarStr << " = ";

  std::set<std::shared_ptr<IndexVar>> rvars;
  for (auto &operand : operands) {
    for (auto &iv : operand.getIndexVariables()) {
      if (iv->getOperator() != IndexVar::Operator::FREE &&
          rvars.find(iv) == rvars.end()) {
        rvars.insert(iv);
        os << *iv << " ";
      }
    }
  }

  unsigned int numOperands = operands.size();
  auto opit = operands.begin();
  if (numOperands == 1) {
    os << opString(op) << *opit++;
  }
  else if (numOperands == 2) {
    os << *opit++ << opString(op) << *opit++;
  } else {
    assert(false && "Not supported yet");
  }
}

void IndexExpr::initType() {
  assert(operands.size() > 0);
  Type ctype = operands[0].getTensor()->getType()->getComponentType();
  std::vector<IndexSetProduct> dimensions;
  for (auto &iv : indexVars) {
    dimensions.push_back(iv->getIndexSet());
  }
  setType(new TensorType(ctype, dimensions));
}


// class Call
void Call::print(std::ostream &os) const {
  os << getName() << "(" << util::join(arguments, ", ") << ")";
}


// class VariableStore
void VariableStore::print(std::ostream &os) const {
  os << getName() << " = " << value->getName();
}


// class Function
void Function::addStatements(const std::vector<std::shared_ptr<IRNode>> &stmts){
  body.insert(body.end(), stmts.begin(), stmts.end());
}

namespace {
class FunctionBodyPrinter : public IRVisitor {
 public:
  FunctionBodyPrinter(std::ostream &os) : IRVisitor(), os(os) {}

  void handle(Function *f) { UNUSED(f); }
  void handle(Argument *t) { UNUSED(t); }
  void handle(Result *t)   { UNUSED(t); }

  void handleDefault(IRNode *t) { os << "  " << *t << endl; }

 private:
  std::ostream &os;
};

} // unnamed namespace

void Function::print(std::ostream &os) const {
  string argumentString = "(" + util::join(this->arguments, ", ") + ")";
  string resultString = (results.size() == 0)
      ? "" : " -> (" + util::join(this->results, ", ") + ")";
  os << "func " << getName() << argumentString << resultString << endl;
  FunctionBodyPrinter fp(os);
  fp.visit((Function*)this);
  os << "end";
}


// class Argument
void Argument::print(std::ostream &os) const {
  TensorNode::print(os);
}


// class Result
void Result::print(std::ostream &os) const {
  TensorNode::print(os);
}


// class Test
void Test::print(std::ostream &os) const {
  std::vector<std::shared_ptr<TensorNode>> args;
  args.insert(args.end(), arguments.begin(), arguments.end());
  Call call(callee, args);
  os << call << " == " << util::join(expected, ", ");
}

}} // namespace simit::internal
