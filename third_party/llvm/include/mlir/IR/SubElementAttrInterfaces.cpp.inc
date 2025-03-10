/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Interface Definitions                                                      *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

/// Walk all of the immediately nested sub-attributes and sub-types. This
/// method does not recurse into sub elements.
void mlir::SubElementAttrInterface::walkImmediateSubElements(llvm::function_ref<void(mlir::Attribute)> walkAttrsFn, llvm::function_ref<void(mlir::Type)> walkTypesFn) const {
      return getImpl()->walkImmediateSubElements(getImpl(), *this, walkAttrsFn, walkTypesFn);
  }
/// Replace the immediately nested sub-attributes and sub-types with those provided.
/// The order of the provided elements is derived from the order of the elements
/// returned by the callbacks of `walkImmediateSubElements`. The element at index 0
/// would replace the very first attribute given by `walkImmediateSubElements`.
/// On success, the new instance with the values replaced is returned. If replacement
/// fails, nullptr is returned.
/// 
/// Note that replacing the sub-elements of mutable types or attributes is
/// not currently supported by the interface. If an implementing type or
/// attribute is mutable, it should return `nullptr` if it has no mechanism
/// for replacing sub elements.
::mlir::Attribute mlir::SubElementAttrInterface::replaceImmediateSubElements(::llvm::ArrayRef<::mlir::Attribute> replAttrs, ::llvm::ArrayRef<::mlir::Type> replTypes) const {
      return getImpl()->replaceImmediateSubElements(getImpl(), *this, replAttrs, replTypes);
  }
