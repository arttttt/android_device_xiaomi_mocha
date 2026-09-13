/*
 * Four LLVM accessors that this board's RenderScript blobs expect to find in
 * the library, and that the library no longer emits.
 *
 * libnvRSCompiler.so was linked against a build of LLVM 3.8 in which these
 * were out-of-line functions. AOSP's external/llvm is the same LLVM 3.8 --
 * kept precisely because RenderScript needs it -- but it declares all four
 * inline, so no symbol is exported and the blob cannot resolve them:
 *
 *     llvm::Type::getVectorNumElements() const
 *     llvm::Type::getPointerAddressSpace() const
 *     llvm::Type::getSequentialElementType() const
 *     llvm::Function::hasGC() const
 *
 * The bodies below are not a reimplementation: each one calls the header's
 * own inline definition, so whatever external/llvm says today is what the
 * blob gets. They are declared with their mangled names and an explicit this
 * because a member function that the header already defines inline cannot be
 * defined again.
 *
 * The other ~130 LLVM symbols the blob wants are exported by libLLVM_android
 * normally, which is why this module links it: the name libLLVM.so, which is
 * what the blob asks for in DT_NEEDED, is answered here, and everything this
 * file does not define is found one library further along.
 */

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

extern "C" {

// llvm::Type::getVectorNumElements() const
unsigned _ZNK4llvm4Type20getVectorNumElementsEv(const llvm::Type *type) {
    return type->getVectorNumElements();
}

// llvm::Type::getPointerAddressSpace() const
unsigned _ZNK4llvm4Type22getPointerAddressSpaceEv(const llvm::Type *type) {
    return type->getPointerAddressSpace();
}

// llvm::Type::getSequentialElementType() const
llvm::Type *_ZNK4llvm4Type24getSequentialElementTypeEv(const llvm::Type *type) {
    return type->getSequentialElementType();
}

// llvm::Function::hasGC() const
bool _ZNK4llvm8Function5hasGCEv(const llvm::Function *function) {
    return function->hasGC();
}

}  // extern "C"
