#include <memory>

#include "const_fold.h"
#include "hir.h"
#include "ir.h"
#include "hir_rewriter.h"

namespace simit {
namespace hir {

void ConstantFolding::visit(NegExpr::Ptr expr) {
  expr->operand = rewrite<Expr>(expr->operand);
  if (isa<IntLiteral>(expr->operand)) {
    const auto operand = to<IntLiteral>(expr->operand);
    operand->val *= -1;
    node = operand;
    return;
  } else if (isa<FloatLiteral>(expr->operand)) {
    const auto operand = to<FloatLiteral>(expr->operand);
    operand->val *= -1.0;
    node = operand;
    return;
  } else if (isa<DenseTensorLiteral>(expr->operand)) {
    class NegateTensorLiteral : public HIRVisitor {
      public:
        void negate(DenseTensorLiteral::Ptr tensor) {
          tensor->accept(this);
        }

      private:
        virtual void visit(IntVectorLiteral::Ptr vec) {
          for (unsigned i = 0; i < vec->vals.size(); ++i) {
            vec->vals[i] *= -1;
          }
        }
        virtual void visit(FloatVectorLiteral::Ptr vec) {
          for (unsigned i = 0; i < vec->vals.size(); ++i) {
            vec->vals[i] *= -1.0;
          }
        }
    };
    const auto operand = to<DenseTensorLiteral>(expr->operand);
    NegateTensorLiteral().negate(operand);
    node = operand;
    return;
  }
  node = expr;
}

void ConstantFolding::visit(TransposeExpr::Ptr expr) {
  expr->operand = rewrite<Expr>(expr->operand);
  if (isa<IntLiteral>(expr->operand) || isa<FloatLiteral>(expr->operand)) {
    node = expr->operand;
    return;
  } else if (isa<DenseTensorLiteral>(expr->operand)) {
    const auto operand = to<DenseTensorLiteral>(expr->operand);
    if (isa<IntVectorLiteral>(operand) || isa<FloatVectorLiteral>(operand)) {
      operand->transposed = !operand->transposed;
      node = operand;
      return;
    }
  }
  node = expr;
}

}
}

